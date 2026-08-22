// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/QuickSlotComponent.h"
#include "Net/UnrealNetwork.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGBaseCharacter.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGGameplayAbility.h"

UQuickSlotComponent::UQuickSlotComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	SkillSlots.SetNum(MaxSkillSlots);
	ItemSlots.SetNum(MaxItemSlots);
}

void UQuickSlotComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UQuickSlotComponent, SkillSlots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UQuickSlotComponent, ItemSlots, COND_OwnerOnly);
}

void UQuickSlotComponent::OnRep_SkillSlots()
{
	for (int32 i = 0; i < SkillSlots.Num(); i++)
	{
		BroadcastSlotChanged(true, i);
	}
}

void UQuickSlotComponent::OnRep_ItemSlots()
{
	for (int32 i = 0; i < ItemSlots.Num(); i++)
	{
		BroadcastSlotChanged(false, i);
	}
}

void UQuickSlotComponent::SetSkillSlot(int32 Index, FGameplayTag AbilityTag)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (!SkillSlots.IsValidIndex(Index)) return;

		if (AbilityTag.IsValid())
		{
			const ARPGBaseCharacter* OwnerCharacter = Cast<ARPGBaseCharacter>(Owner);
			URPGAbilitySystemComponent* ASC =
				OwnerCharacter ? OwnerCharacter->GetRPGAbilitySystemComponent() : nullptr;
			TArray<FGameplayAbilitySpec*> MatchingAbilitySpecs;
			if (ASC)
			{
				ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(
					AbilityTag.GetSingleTagContainer(), MatchingAbilitySpecs);
			}

			if (MatchingAbilitySpecs.IsEmpty())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("QuickSlot: Rejected an ability tag that is not granted to the owner."));
				return;
			}
		}

		if (SkillSlots[Index].AbilityTag.MatchesTagExact(AbilityTag)
			&& SkillSlots[Index].Item == nullptr)
		{
			return;
		}

		SkillSlots[Index].AbilityTag = AbilityTag;
		SkillSlots[Index].Item = nullptr;
		BroadcastSlotChanged(true, Index);
		Owner->ForceNetUpdate();
	}
	else
	{
		Server_SetSkillSlot(Index, AbilityTag);
	}
}

void UQuickSlotComponent::Server_SetSkillSlot_Implementation(int32 Index, FGameplayTag AbilityTag)
{
	SetSkillSlot(Index, AbilityTag);
}

void UQuickSlotComponent::SetItemSlot(int32 Index, URPGItemBase* NewItem)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (!ItemSlots.IsValidIndex(Index)) return;

		// 소모성 아이템만 등록 가능하도록 제한
		if (NewItem && !NewItem->IsConsumable())
		{
			UE_LOG(LogTemp, Warning, TEXT("QuickSlot: Only consumable items can be registered."));
			return;
		}

		if (NewItem)
		{
			URPGInventoryComponent* Inventory = BoundInventory.Get();
			if (!Inventory)
			{
				const APawn* OwnerPawn = Cast<APawn>(Owner);
				APlayerController* PlayerController =
					OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
				Inventory =
					URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(
						PlayerController);
				BindInventory(Inventory);
			}

			if (!Inventory || !Inventory->GetAllItems().Contains(NewItem))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("QuickSlot: Rejected an item that is not owned by this inventory."));
				return;
			}
		}

		if (ItemSlots[Index].Item == NewItem
			&& !ItemSlots[Index].AbilityTag.IsValid())
		{
			return;
		}

		ItemSlots[Index].Item = NewItem;
		ItemSlots[Index].AbilityTag = FGameplayTag::EmptyTag;
		BroadcastSlotChanged(false, Index);
		Owner->ForceNetUpdate();
	}
	else
	{
		Server_SetItemSlot(Index, NewItem);
	}
}

void UQuickSlotComponent::Server_SetItemSlot_Implementation(int32 Index, URPGItemBase* NewItem)
{
	SetItemSlot(Index, NewItem);
}

void UQuickSlotComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MaxSkillSlots = FMath::Max(MaxSkillSlots, 1);
		MaxItemSlots = FMath::Max(MaxItemSlots, 1);
		SkillSlots.SetNum(MaxSkillSlots);
		ItemSlots.SetNum(MaxItemSlots);
	}

	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
		BindInventory(
			URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC));
	}
}

void UQuickSlotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInventory();
	Super::EndPlay(EndPlayReason);
}

void UQuickSlotComponent::BindInventory(URPGInventoryComponent* Inventory)
{
	if (BoundInventory.Get() == Inventory)
	{
		return;
	}

	UnbindInventory();
	if (!IsValid(Inventory))
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || Inventory->GetOwner() != OwnerPawn->GetController())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("QuickSlot: Rejected an inventory that does not belong to the owning pawn."));
		return;
	}

	BoundInventory = Inventory;
	Inventory->OnItemUpdated.AddUniqueDynamic(
		this, &UQuickSlotComponent::HandleOnItemQuantityChanged);
	Inventory->OnItemRemoved.AddUniqueDynamic(
		this, &UQuickSlotComponent::HandleOnItemRemoved);
}

void UQuickSlotComponent::UnbindInventory()
{
	if (URPGInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnItemUpdated.RemoveDynamic(
			this, &UQuickSlotComponent::HandleOnItemQuantityChanged);
		Inventory->OnItemRemoved.RemoveDynamic(
			this, &UQuickSlotComponent::HandleOnItemRemoved);
	}

	BoundInventory.Reset();
}

void UQuickSlotComponent::HandleOnItemRemoved(URPGItemBase* RemovedItem)
{
	if (!IsValid(RemovedItem) || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (int32 i = 0; i < ItemSlots.Num(); i++)
	{
		if (ItemSlots[i].Item == RemovedItem)
		{
			ClearSlot(false, i);
		}
	}
}

void UQuickSlotComponent::HandleOnItemQuantityChanged(URPGItemBase* ChangedItem)
{
	if (!IsValid(ChangedItem))
	{
		return;
	}

	const bool bIsAssigned = ItemSlots.ContainsByPredicate(
		[ChangedItem](const FRPGQuickSlotContent& Content)
	{
		return Content.Item == ChangedItem;
	});

	if (bIsAssigned)
	{
		OnQuickSlotQuantityChanged.Broadcast(ChangedItem, ChangedItem->GetTotalQuantity());
	}
}

void UQuickSlotComponent::UseSkillSlot(int32 Index)
{
	BeginUseSkillSlot(Index);
	EndUseSkillSlot(Index);
}

void UQuickSlotComponent::BeginUseSkillSlot(const int32 Index)
{
	if (!SkillSlots.IsValidIndex(Index) || SkillSlots[Index].AbilityTag.IsValid() == false) return;

	if (ARPGBaseCharacter* OwnerCharacter = Cast<ARPGBaseCharacter>(GetOwner()))
	{
		if (URPGAbilitySystemComponent* ASC = OwnerCharacter->GetRPGAbilitySystemComponent())
		{
			const FGameplayAbilitySpecHandle SpecHandle =
				ASC->FindUniqueAbilitySpecHandleByTag(SkillSlots[Index].AbilityTag);
			if (SpecHandle.IsValid())
			{
				PressedSkillSpecHandles.Add(Index, SpecHandle);
				ASC->OnAbilitySpecInputPressed(SpecHandle);
			}
		}
	}
}

void UQuickSlotComponent::EndUseSkillSlot(const int32 Index)
{
	ARPGBaseCharacter* OwnerCharacter = Cast<ARPGBaseCharacter>(GetOwner());
	URPGAbilitySystemComponent* ASC =
		OwnerCharacter ? OwnerCharacter->GetRPGAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		PressedSkillSpecHandles.Remove(Index);
		return;
	}

	FGameplayAbilitySpecHandle SpecHandle;
	if (const FGameplayAbilitySpecHandle* PressedHandle =
		PressedSkillSpecHandles.Find(Index))
	{
		SpecHandle = *PressedHandle;
	}
	else if (SkillSlots.IsValidIndex(Index))
	{
		SpecHandle = ASC->FindUniqueAbilitySpecHandleByTag(
			SkillSlots[Index].AbilityTag);
	}

	PressedSkillSpecHandles.Remove(Index);
	if (SpecHandle.IsValid())
	{
		ASC->OnAbilitySpecInputReleased(SpecHandle);
	}
}

void UQuickSlotComponent::UseItemSlot(int32 Index, const APlayerController* PC)
{
	if (!ItemSlots.IsValidIndex(Index) || !IsValid(ItemSlots[Index].Item)) return;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerController* OwnerController =
		OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerController || (PC && PC != OwnerController))
	{
		return;
	}

	URPGInventoryComponent* InventoryComponent = BoundInventory.Get();
	if (!InventoryComponent)
	{
		InventoryComponent =
			URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(
				OwnerController);
		BindInventory(InventoryComponent);
	}

	if (InventoryComponent
		&& InventoryComponent->GetAllItems().Contains(ItemSlots[Index].Item))
	{
		InventoryComponent->Server_ConsumeItem(ItemSlots[Index].Item);
	}
}

void UQuickSlotComponent::ClearSlot(bool bIsSkillSlot, int32 Index)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (bIsSkillSlot)
		{
			if (!SkillSlots.IsValidIndex(Index)) return;
			if (SkillSlots[Index].IsEmpty()) return;
			SkillSlots[Index] = FRPGQuickSlotContent();
			BroadcastSlotChanged(true, Index);
		}
		else
		{
			if (!ItemSlots.IsValidIndex(Index)) return;
			if (ItemSlots[Index].IsEmpty()) return;
			ItemSlots[Index] = FRPGQuickSlotContent();
			BroadcastSlotChanged(false, Index);
		}

		Owner->ForceNetUpdate();
	}
	else
	{
		Server_ClearSlot(bIsSkillSlot, Index);
	}
}

void UQuickSlotComponent::Server_ClearSlot_Implementation(bool bIsSkillSlot, int32 Index)
{
	ClearSlot(bIsSkillSlot, Index);
}

void UQuickSlotComponent::BroadcastSlotChanged(bool bIsSkillSlot, int32 Index)
{
	const TArray<FRPGQuickSlotContent>& Slots = bIsSkillSlot ? SkillSlots : ItemSlots;
	if (!Slots.IsValidIndex(Index))
	{
		return;
	}

	if (bIsSkillSlot)
	{
		OnSkillSlotChanged.Broadcast(Index, Slots[Index]);
	}
	else
	{
		OnItemSlotChanged.Broadcast(Index, Slots[Index]);
	}

	OnQuickSlotChanged.Broadcast(Index, Slots[Index]);
}

const FRPGQuickSlotContent* UQuickSlotComponent::GetSkillSlotContent(int32 Index) const
{
	if (SkillSlots.IsValidIndex(Index)) return &SkillSlots[Index];
	return nullptr;
}

const FRPGQuickSlotContent* UQuickSlotComponent::GetItemSlotContent(int32 Index) const
{
	if (ItemSlots.IsValidIndex(Index)) return &ItemSlots[Index];
	return nullptr;
}

UTexture2D* UQuickSlotComponent::GetSkillIcon(FGameplayTag AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return nullptr;
	}

	const ARPGBaseCharacter* OwnerCharacter = Cast<ARPGBaseCharacter>(GetOwner());
	URPGAbilitySystemComponent* ASC =
		OwnerCharacter ? OwnerCharacter->GetRPGAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return nullptr;
	}

	TArray<FGameplayAbilitySpec*> MatchingAbilitySpecs;
	ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTag.GetSingleTagContainer(), MatchingAbilitySpecs);
	for (const FGameplayAbilitySpec* AbilitySpec : MatchingAbilitySpecs)
	{
		const URPGGameplayAbility* Ability =
			AbilitySpec ? Cast<URPGGameplayAbility>(AbilitySpec->Ability) : nullptr;
		if (Ability && Ability->GetAbilityIcon())
		{
			return Ability->GetAbilityIcon();
		}
	}

	return nullptr;
}
