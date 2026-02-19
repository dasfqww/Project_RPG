// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/QuickSlotComponent.h"
#include "Net/UnrealNetwork.h"
#include "UI/RPGQuickSlotWidget.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "UI/RPGWidgetBase.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGAbilitySystemComponent.h"

#include "RPGDebugHelper.h"

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

	DOREPLIFETIME(UQuickSlotComponent, SkillSlots);
	DOREPLIFETIME(UQuickSlotComponent, ItemSlots);
}

void UQuickSlotComponent::OnRep_SkillSlots()
{
	for (int32 i = 0; i < SkillSlots.Num(); i++)
	{
		OnSkillSlotChanged.Broadcast(i, SkillSlots[i]);
	}
}

void UQuickSlotComponent::OnRep_ItemSlots()
{
	for (int32 i = 0; i < ItemSlots.Num(); i++)
	{
		OnItemSlotChanged.Broadcast(i, ItemSlots[i]);
	}
}

void UQuickSlotComponent::SetSkillSlot(int32 Index, FGameplayTag AbilityTag)
{
	if (GetOwner()->HasAuthority())
	{
		if (!SkillSlots.IsValidIndex(Index)) return;

		SkillSlots[Index].AbilityTag = AbilityTag;
		SkillSlots[Index].Item = nullptr;
		OnSkillSlotChanged.Broadcast(Index, SkillSlots[Index]);
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
	if (GetOwner()->HasAuthority())
	{
		if (!ItemSlots.IsValidIndex(Index)) return;

		// 소모성 아이템만 등록 가능하도록 제한
		if (NewItem && !NewItem->IsConsumable())
		{
			UE_LOG(LogTemp, Warning, TEXT("QuickSlot: Only consumable items can be registered."));
			return;
		}

		ItemSlots[Index].Item = NewItem;
		ItemSlots[Index].AbilityTag = FGameplayTag::EmptyTag;
		OnItemSlotChanged.Broadcast(Index, ItemSlots[Index]);
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

	// 인벤토리 컴포넌트 구독
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());

		if (URPGInventoryComponent* Inventory = URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC))
		{
			Inventory->OnQuantityChanged.AddDynamic(this, &UQuickSlotComponent::HandleOnItemQuantityChanged);
			Inventory->OnItemRemoved.AddDynamic(this, &UQuickSlotComponent::HandleOnItemRemoved);
		}
	}
}

void UQuickSlotComponent::HandleOnItemRemoved(URPGItemBase* RemovedItem)
{
	if (!RemovedItem) return;

	for (int32 i = 0; i < ItemSlots.Num(); i++)
	{
		if (ItemSlots[i].Item == RemovedItem)
		{
			ClearSlot(false, i);
		}
	}
}

void UQuickSlotComponent::HandleOnItemQuantityChanged(const FSlotAvailabilityResult& Result)
{
	URPGItemBase* ChangedItem = Result.Item.Get();
	if (!ChangedItem) return;

	for (int32 i = 0; i < ItemSlots.Num(); i++)
	{
		if (ItemSlots[i].Item == ChangedItem)
		{
			OnQuickSlotQuantityChanged.Broadcast(ChangedItem, ChangedItem->GetTotalQuantity());
		}
	}
}

void UQuickSlotComponent::UseSkillSlot(int32 Index)
{
	if (!SkillSlots.IsValidIndex(Index) || SkillSlots[Index].AbilityTag.IsValid() == false) return;

	if (ARPGBaseCharacter* OwnerCharacter = Cast<ARPGBaseCharacter>(GetOwner()))
	{
		if (URPGAbilitySystemComponent* ASC = OwnerCharacter->GetRPGAbilitySystemComponent())
		{
			// GAS 입력 시스템을 통해 스킬 발동 (이미 해당 태그로 바인딩된 능력이 있다면 발동됨)
			ASC->OnAbilityInputPressed(SkillSlots[Index].AbilityTag);
			ASC->OnAbilityInputReleased(SkillSlots[Index].AbilityTag);
		}
	}
}

void UQuickSlotComponent::UseItemSlot(int32 Index, const APlayerController* PC)
{
	if (!ItemSlots.IsValidIndex(Index) || ItemSlots[Index].Item == nullptr) return;

	if (URPGInventoryComponent* InventoryComponent = URPGCoreFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC))
	{
		InventoryComponent->Server_ConsumeItem(ItemSlots[Index].Item);
	}
}

void UQuickSlotComponent::ClearSlot(bool bIsSkillSlot, int32 Index)
{
	if (GetOwner()->HasAuthority())
	{
		if (bIsSkillSlot)
		{
			if (!SkillSlots.IsValidIndex(Index)) return;
			SkillSlots[Index] = FRPGQuickSlotContent();
			OnSkillSlotChanged.Broadcast(Index, SkillSlots[Index]);
		}
		else
		{
			if (!ItemSlots.IsValidIndex(Index)) return;
			ItemSlots[Index] = FRPGQuickSlotContent();
			OnItemSlotChanged.Broadcast(Index, ItemSlots[Index]);
		}
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