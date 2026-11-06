// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/QuickSlotComponent.h"
#include "UI/ActionBar/RPGActionBarContext.h"
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/DataManager.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Skill/QuickSlot/RPGSkillQuickSlotServices.h"
#include "Skill/RPGClassSkillEligibility.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "PlayerClass/RPGPlayerClassComponent.h"

namespace
{
URPGAbilitySystemComponent* GetQuickSlotASC(const UQuickSlotComponent& Slots)
{
	return Cast<URPGAbilitySystemComponent>(
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Slots.GetOwner()));
}
}

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
	DOREPLIFETIME_CONDITION(UQuickSlotComponent, SkillSlotsRevision, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UQuickSlotComponent, bSkillSlotsInitialized, COND_OwnerOnly);
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
	if (!Owner || (ActionBarContext && ActionBarContext->HasOverride()) || !bSkillSlotsInitialized || !SkillSlots.IsValidIndex(Index))
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		if (AbilityTag.IsValid() && !CanAssignSkillSlot(Index, AbilityTag))
		{
			UE_LOG(LogTemp, Warning, TEXT("QuickSlot: Rejected an unowned or ambiguous skill."));
			return;
		}

		if (SkillSlots[Index].AbilityTag.MatchesTagExact(AbilityTag)
			&& !SkillSlots[Index].ItemId.IsValid())
		{
			return;
		}
		if (!RPGClassSkillEligibility::CanChangeConfiguration(Owner)) return;

		// One registration per skill. Dropping an already registered skill moves
		// it atomically; an occupied destination swaps instead of losing a skill.
		const int32 SourceIndex = AbilityTag.IsValid()
			? SkillSlots.IndexOfByPredicate([AbilityTag](const FRPGQuickSlotContent& Slot)
				{ return Slot.AbilityTag == AbilityTag; }) : INDEX_NONE;
		if (SourceIndex != INDEX_NONE && SourceIndex != Index)
		{
			Swap(SkillSlots[SourceIndex], SkillSlots[Index]);
			BroadcastSlotChanged(true, SourceIndex);
			BroadcastSlotChanged(true, Index);
			Owner->ForceNetUpdate();
			return;
		}
		SkillSlots[Index] = FRPGQuickSlotContent();
		SkillSlots[Index].AbilityTag = AbilityTag;
		BroadcastSlotChanged(true, Index);
		Owner->ForceNetUpdate();
	}
	else
	{
		Server_SetSkillSlot(Index, AbilityTag);
	}
}

bool UQuickSlotComponent::AdmitSlotRequest()
{
	return GetOwner() && GetOwner()->HasAuthority() &&
		SlotRequestBudget.TryConsume(FPlatformTime::Seconds());
}

void UQuickSlotComponent::Server_SetSkillSlot_Implementation(int32 Index, FGameplayTag AbilityTag)
{
	// Editor script dispatch can execute an RPC locally on a client. Do not
	// route that invocation back through the client request path recursively.
	if (!AdmitSlotRequest()) return;
	SetSkillSlot(Index, AbilityTag);
	++SkillSlotsRevision;
	GetOwner()->ForceNetUpdate();
}

void UQuickSlotComponent::SetItemSlot(int32 Index, URPGItemBase* NewItem)
{
	// Compatibility with existing Blueprint pins, not a legacy state/write path.
	SetItemSlotById(Index, IsValid(NewItem) ? NewItem->GetInstanceId() : FGuid());
}

void UQuickSlotComponent::SetItemSlotById(int32 Index, FGuid ItemId)
{
	AActor* Owner = GetOwner();
	if (!Owner || !ItemSlots.IsValidIndex(Index))
	{
		return;
	}

	if (Owner->HasAuthority())
	{
		EnsureInventoryProjectionBinding();
		if (ItemId.IsValid())
		{
			FRPGInventoryProjectionEntry Entry;
			FRPGItemActionPolicy Policy;
			const URPGInventoryProjectionComponent* Projection = BoundProjection.Get();
			if (!Projection || !Projection->GetAuthoritativeRecordSource() ||
				!Projection->FindProjectedItem(ItemId, Entry) ||
				Entry.GetContainerType() != ERPGItemContainerType::Inventory ||
				Entry.GetQuantity() <= 0 || !ActionPolicies ||
				!ActionPolicies->TryFindActionPolicy(Entry.GetDefinitionId(), Policy) ||
				!Policy.IsValid() || !Policy.IsConsumable() ||
				Policy.DefinitionId != Entry.GetDefinitionId() ||
				Policy.DefinitionVersion != Entry.GetDefinitionVersion())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("QuickSlot: Rejected an ID without an owned consumable inventory record."));
				return;
			}
		}

		if (ItemSlots[Index].ItemId == ItemId
			&& !ItemSlots[Index].AbilityTag.IsValid())
		{
			return;
		}

		ItemSlots[Index] = FRPGQuickSlotContent();
		ItemSlots[Index].ItemId = ItemId;
		BroadcastSlotChanged(false, Index);
		Owner->ForceNetUpdate();
	}
	else
	{
		Server_SetItemSlotById(Index, ItemId);
	}
}

void UQuickSlotComponent::Server_SetItemSlotById_Implementation(int32 Index, FGuid ItemId)
{
	if (!AdmitSlotRequest()) return;
	SetItemSlotById(Index, ItemId);
}

void UQuickSlotComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MaxSkillSlots = FMath::Clamp(MaxSkillSlots, 1, URPGPlayerClassDefinition::MaxCombatSkills);
		MaxItemSlots = FMath::Max(MaxItemSlots, 1);
		SkillSlots.SetNum(MaxSkillSlots);
		ItemSlots.SetNum(MaxItemSlots);
	}

	EnsureInventoryProjectionBinding();
}

void UQuickSlotComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAllSkillInputs();
	if (ActionBarContext) ActionBarContext->Shutdown();
	UnbindInventoryProjection();
	Super::EndPlay(EndPlayReason);
}

void UQuickSlotComponent::BindInventoryProjection(
	URPGInventoryProjectionComponent* Projection,
	const IRPGItemActionPolicyCatalog* Policies)
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (Projection && (!OwnerPawn || Projection->GetOwner() != OwnerPawn->GetController()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("QuickSlot: Rejected a projection belonging to another controller."));
		return;
	}
	if (BoundProjection.Get() == Projection && (!Policies || ActionPolicies == Policies))
	{
		return;
	}
	UnbindInventoryProjection();
	if (!IsValid(Projection))
	{
		return;
	}
	BoundProjection = Projection;
	UDataManager* DataManager = GetWorld() && GetWorld()->GetGameInstance()
		? GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	ActionPolicies = Policies ? Policies
		: (DataManager ? &DataManager->GetItemActionPolicyCatalog() : nullptr);
	Projection->OnProjectionChanged.AddUniqueDynamic(
		this, &ThisClass::HandleProjectionChanged);
	Projection->OnLoadStateChanged.AddUniqueDynamic(
		this, &ThisClass::HandleProjectionLoadStateChanged);
	HandleProjectionChanged();
}

void UQuickSlotComponent::UnbindInventoryProjection()
{
	if (URPGInventoryProjectionComponent* Projection = BoundProjection.Get())
	{
		Projection->OnProjectionChanged.RemoveDynamic(this, &ThisClass::HandleProjectionChanged);
		Projection->OnLoadStateChanged.RemoveDynamic(this, &ThisClass::HandleProjectionLoadStateChanged);
	}
	BoundProjection.Reset();
	ActionPolicies = nullptr;
}

void UQuickSlotComponent::EnsureInventoryProjectionBinding()
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr;
	BindInventoryProjection(Controller
		? Controller->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr);
}

bool UQuickSlotComponent::FindAssignedItem(int32 Index, FRPGInventoryProjectionEntry& OutEntry) const
{
	OutEntry = FRPGInventoryProjectionEntry();
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const URPGInventoryProjectionComponent* Projection = BoundProjection.Get();
	return ItemSlots.IsValidIndex(Index) && ItemSlots[Index].ItemId.IsValid() &&
		Projection && OwnerPawn && Projection->GetOwner() == OwnerPawn->GetController() &&
		Projection->FindProjectedItem(ItemSlots[Index].ItemId, OutEntry) &&
		OutEntry.GetContainerType() == ERPGItemContainerType::Inventory && OutEntry.GetQuantity() > 0;
}

void UQuickSlotComponent::HandleProjectionChanged()
{
	const URPGInventoryProjectionComponent* Projection = BoundProjection.Get();
	const bool bCanClear = GetOwner() && GetOwner()->HasAuthority() && Projection &&
		Projection->GetAuthoritativeRecordSource() &&
		Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Loading;
	for (int32 Index = 0; Index < ItemSlots.Num(); ++Index)
	{
		FRPGInventoryProjectionEntry Entry;
		if (bCanClear && ItemSlots[Index].ItemId.IsValid() && !FindAssignedItem(Index, Entry))
		{
			ClearSlot(false, Index);
		}
		else
		{
			// Also refresh on clients when projection and shortcut replication arrive in either order.
			BroadcastSlotChanged(false, Index);
		}
	}
}

void UQuickSlotComponent::HandleProjectionLoadStateChanged(ERPGInventoryProjectionLoadState LoadState)
{
	HandleProjectionChanged();
}

void UQuickSlotComponent::UseSkillSlot(int32 Index)
{
	BeginUseSkillSlot(Index);
	EndUseSkillSlot(Index);
}

URPGActionBarContext* UQuickSlotComponent::GetActionBarContext()
{
	if (!ActionBarContext) ActionBarContext = NewObject<URPGActionBarContext>(this);
	return ActionBarContext;
}

void UQuickSlotComponent::BeginUseSkillSlot(const int32 Index)
{
	if (Index < 0 || Index >= MaxSkillSlots || HeldSkillInputs.Contains(Index)) return;
	HeldSkillInputs.Add(Index);
	if (ActionBarContext && ActionBarContext->ConsumesCombatInput())
	{
		ActionBarContext->PressSlot(Index);
		return;
	}
	if (!SkillSlots.IsValidIndex(Index) || SkillSlots[Index].AbilityTag.IsValid() == false) return;
	FRPGGASSkillQuickSlotAccess Access(GetQuickSlotASC(*this));
	FRPGSkillQuickSlotInputRouter::Press(PressedSkillSpecHandles, Index,
		Access.Resolve(SkillSlots[Index].AbilityTag), Access);
}

void UQuickSlotComponent::EndUseSkillSlot(const int32 Index)
{
	HeldSkillInputs.Remove(Index);
	if (ActionBarContext) ActionBarContext->ReleaseSlot(Index);
	FRPGGASSkillQuickSlotAccess Access(GetQuickSlotASC(*this));
	FRPGSkillQuickSlotInputRouter::Release(PressedSkillSpecHandles, Index, Access);
}

void UQuickSlotComponent::ReleaseAllSkillInputs(bool bKeepPhysicalState)
{
	if (!bKeepPhysicalState) HeldSkillInputs.Reset();
	if (ActionBarContext) ActionBarContext->ReleaseAll(bKeepPhysicalState);
	FRPGGASSkillQuickSlotAccess Access(GetQuickSlotASC(*this));
	FRPGSkillQuickSlotInputRouter::ReleaseAll(PressedSkillSpecHandles, Access);
}

bool UQuickSlotComponent::CanAssignSkillSlot(const int32 Index, const FGameplayTag SkillTag) const
{
	return !(ActionBarContext && ActionBarContext->HasOverride()) && bSkillSlotsInitialized && SkillSlots.IsValidIndex(Index) && SkillTag.IsValid() &&
		FRPGGASSkillQuickSlotAccess(GetQuickSlotASC(*this)).Resolve(SkillTag).IsValid();
}

bool UQuickSlotComponent::IsSkillEquipped(const FGameplayTag SkillTag) const
{
	if (!bSkillSlotsInitialized || !SkillTag.IsValid() || SkillSlots.Num() > URPGPlayerClassDefinition::MaxCombatSkills) return false;
	int32 Count = 0;
	for (const FRPGQuickSlotContent& Slot : SkillSlots)
		if (Slot.AbilityTag == SkillTag && !Slot.ItemId.IsValid()) ++Count;
	return Count == 1;
}

bool UQuickSlotComponent::InitializeSkillSlotsFromRecord(const uint32 ExpectedSkillRestoreGeneration,
	const TArray<FGameplayTag>& SlotTags, FText& OutError)
{
	OutError = FText::GetEmpty();
	AActor* Owner = GetOwner();
	const auto* Progression = Owner ? Owner->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* ClassOwner = Owner ? Owner->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	const int32 Capacity = FMath::Clamp(MaxSkillSlots, 1, URPGPlayerClassDefinition::MaxCombatSkills);
	if (!Owner || !Owner->HasAuthority() || bSkillSlotsInitialized || !Progression
		|| !ClassOwner || ClassOwner->GetApplyState() != ERPGPlayerClassApplyState::Applied
		|| !Progression->GetPlayerClassDefinition() || ExpectedSkillRestoreGeneration == 0
		|| Progression->GetSkillRestoreGeneration() != ExpectedSkillRestoreGeneration || SlotTags.Num() > Capacity
		|| !RPGClassSkillEligibility::CanRestoreConfiguration(Owner))
	{
		OutError = FText::FromString(TEXT("Skill slots require one current, ready server restore and a valid capacity."));
		return false;
	}
	TArray<FRPGQuickSlotContent> RestoredSlots;
	RestoredSlots.SetNum(Capacity);
	TSet<FGameplayTag> Seen;
	FRPGGASSkillQuickSlotAccess Access(GetQuickSlotASC(*this));
	for (int32 Index = 0; Index < SlotTags.Num(); ++Index)
	{
		const FGameplayTag Tag = SlotTags[Index];
		if (!Tag.IsValid()) continue;
		if (Seen.Contains(Tag) || !Access.Resolve(Tag).IsValid())
		{
			OutError = FText::FromString(TEXT("Restored slots must reference unique, acquired, class-owned ability specs."));
			return false;
		}
		Seen.Add(Tag);
		RestoredSlots[Index].AbilityTag = Tag;
	}
	// Commit the entire array before any delegate can reenter; never populate missing slots from defaults.
	SkillSlots = MoveTemp(RestoredSlots);
	bSkillSlotsInitialized = true;
	++SkillSlotsRevision;
	Owner->ForceNetUpdate();
	for (int32 Index = 0; Index < SkillSlots.Num(); ++Index) BroadcastSlotChanged(true, Index);
	return true;
}

FRPGSkillQuickSlotPresentation UQuickSlotComponent::ReadSkillPresentation(const FGameplayTag SkillTag) const
{
	return FRPGGASSkillQuickSlotAccess(GetQuickSlotASC(*this)).Read(SkillTag);
}

void UQuickSlotComponent::UseItemSlot(int32 Index, const APlayerController* PC)
{
	if (!ItemSlots.IsValidIndex(Index) || !ItemSlots[Index].ItemId.IsValid()) return;

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	ARPGPlayerController* OwnerController =
		OwnerPawn ? Cast<ARPGPlayerController>(OwnerPawn->GetController()) : nullptr;
	if (!OwnerController || (PC && PC != OwnerController))
	{
		return;
	}

	EnsureInventoryProjectionBinding();
	FRPGInventoryProjectionEntry Entry;
	if (!FindAssignedItem(Index, Entry)) return;
	// Keyboard and widget clicks must reserve the same owner-scoped item as bag actions.
	// Do not fall back to a direct RPC when the local presentation is unavailable.
	FGuid RequestId;
	OwnerController->TryConsumeInventoryItem(Entry.GetItemId(), RequestId);
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
			if (ActionBarContext && ActionBarContext->HasOverride()) return;
			if (!bSkillSlotsInitialized) return;
			if (!SkillSlots.IsValidIndex(Index)) return;
			if (SkillSlots[Index].IsEmpty()) return;
			if (!RPGClassSkillEligibility::CanChangeConfiguration(Owner)) return;
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
	if (!AdmitSlotRequest()) return;
	ClearSlot(bIsSkillSlot, Index);
	if (bIsSkillSlot)
	{
		++SkillSlotsRevision;
		GetOwner()->ForceNetUpdate();
	}
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
	return ReadSkillPresentation(AbilityTag).Icon;
}
