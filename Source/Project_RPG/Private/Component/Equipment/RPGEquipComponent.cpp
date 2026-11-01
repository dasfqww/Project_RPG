#include "Component/Equipment/RPGEquipComponent.h"

#include "AbilitySystemComponent.h"
#include "Character/RPGPlayer.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Controller/RPGPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/RPGItemBase.h"

namespace
{
bool ResolveCommandContext(const URPGEquipComponent& Facade,
	URPGItemCommandComponent*& OutCommands, const IRPGItemRecordSource*& OutRecords)
{
	OutCommands = nullptr;
	OutRecords = nullptr;
	const APawn* Pawn = Cast<APawn>(Facade.GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Pawn->HasAuthority() || !Controller || Controller->GetPawn() != Pawn) return false;
	const auto* Projection = Controller->FindComponentByClass<URPGInventoryProjectionComponent>();
	OutCommands = Controller->FindComponentByClass<URPGItemCommandComponent>();
	OutRecords = Projection ? Projection->GetAuthoritativeRecordSource() : nullptr;
	return OutCommands && OutRecords;
}

void RejectLegacyEquip(const URPGEquipComponent& Facade)
{
	UE_LOG(LogTemp, Warning, TEXT("Legacy equipment request rejected for %s: no canonical item command is available."),
		*GetNameSafe(Facade.GetOwner()));
}
}

// Retain serialized struct types, but never let their callbacks apply legacy effects.
void FRPGEquipList::PostReplicatedAdd(const TArrayView<int32>, int32) {}
void FRPGEquipList::PostReplicatedChange(const TArrayView<int32>, int32) {}
void FRPGEquipList::PostReplicatedRemove(const TArrayView<int32>, int32) {}

URPGEquipComponent::URPGEquipComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void URPGEquipComponent::OnRegister()
{
	Super::OnRegister();
	BindNativeEquipment();
}

void URPGEquipComponent::BeginPlay()
{
	Super::BeginPlay();
	BindNativeEquipment(); // Also handles a native component registered after this facade.
}

void URPGEquipComponent::OnUnregister()
{
	UnbindNativeEquipment();
	Super::OnUnregister();
}

void URPGEquipComponent::BindNativeEquipment()
{
	UnbindNativeEquipment();
	URPGAuthoritativeEquipmentComponent* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (Native)
	{
		BoundEquipment = Native;
		ActiveStateChangedHandle = Native->OnActiveEquipStateChanged.AddUObject(
			this, &ThisClass::HandleNativeStateChanged);
	}
}

void URPGEquipComponent::UnbindNativeEquipment()
{
	if (URPGAuthoritativeEquipmentComponent* Native = BoundEquipment.Get())
	{
		Native->OnActiveEquipStateChanged.Remove(ActiveStateChangedHandle);
	}
	ActiveStateChangedHandle.Reset();
	BoundEquipment.Reset();
}

void URPGEquipComponent::HandleNativeStateChanged(const EEquipState OldState, const EEquipState NewState)
{
	OnEquipStateChanged.Broadcast(OldState, NewState);
}

void URPGEquipComponent::EquipItem(const EEquipmentSlotType SlotType, URPGItemBase* Item)
{
	URPGItemCommandComponent* Commands;
	const IRPGItemRecordSource* Records;
	FRPGItemRecord Record;
	if (static_cast<uint8>(SlotType) >= static_cast<uint8>(EEquipmentSlotType::Count)
		|| !IsValid(Item) || !ResolveCommandContext(*this, Commands, Records)
		|| !Records->Find(Item->GetInstanceId(), Record)
		|| Record.GetLifecycleState() != ERPGItemLifecycleState::Active
		|| Record.GetLocation().ContainerType != ERPGItemContainerType::Inventory)
	{
		RejectLegacyEquip(*this);
		return;
	}
	// The object carries only an ID. Native planning validates owner, policy and occupancy;
	// an occupied slot is NOT silently unequipped as it was in the retired implementation.
	Commands->ServerEquipItem(FGuid::NewGuid(), Record.GetItemId(), Record.GetRevision(), SlotType);
}

void URPGEquipComponent::UnequipItem(const EEquipmentSlotType SlotType)
{
	URPGItemCommandComponent* Commands;
	const IRPGItemRecordSource* Records;
	FRPGEquipmentSnapshot Snapshot;
	FRPGItemRecord Record;
	const auto* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (!Native || !ResolveCommandContext(*this, Commands, Records)
		|| !Native->FindSnapshotInSlot(SlotType, Snapshot)
		|| !Records->Find(Snapshot.ItemId, Record)
		|| Record.GetLifecycleState() != ERPGItemLifecycleState::Active
		|| Record.GetLocation().ContainerType != ERPGItemContainerType::Equipment
		|| Record.GetLocation().SlotIndex != static_cast<int32>(SlotType))
	{
		RejectLegacyEquip(*this);
		return;
	}
	FRPGItemLocation Destination;
	Destination.ContainerType = ERPGItemContainerType::Inventory;
	Destination.ContainerId = Record.GetOwner().OwnerId;
	for (int32 Index = 0; Index < RPGItemInventoryPolicy::SlotCapacity; ++Index)
	{
		Destination.SlotIndex = Index;
		FRPGItemRecord Occupant;
		if (!Records->FindAtLocation(Record.GetOwner(), Destination, Occupant))
		{
			Commands->ServerUnequipItem(FGuid::NewGuid(), Record.GetItemId(), Record.GetRevision(), Index);
			return; // Backend CAS/slot validation resolves any concurrent change.
		}
	}
	RejectLegacyEquip(*this); // Full bag: no state change, actor destruction or grant removal.
}

void URPGEquipComponent::ChangeEquipState(const EEquipState NewState)
{
	if (URPGAuthoritativeEquipmentComponent* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr)
	{
		Native->SetActiveEquipState(NewState); // This remains authority-only; no new RPC.
	}
}

void URPGEquipComponent::UpdateEquippedItems()
{
	// Retired: visibility and applied resources are owned by native equipment only.
}

ARPGPlayer* URPGEquipComponent::GetRPGPlayer() const
{
	return Cast<ARPGPlayer>(GetOwner());
}

ARPGPlayerController* URPGEquipComponent::GetRPGPlayerController() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return Pawn ? Cast<ARPGPlayerController>(Pawn->GetController()) : nullptr;
}

UAbilitySystemComponent* URPGEquipComponent::GetAbilitySystemComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
}

URPGItemBase* URPGEquipComponent::GetItemInSlot(EEquipmentSlotType) const
{
	return nullptr; // Never synthesize a second mutable item object.
}

AActor* URPGEquipComponent::GetSpawnedActorInSlot(const EEquipmentSlotType SlotType) const
{
	const auto* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	FRPGEquipmentSnapshot Snapshot;
	return Native && Native->FindSnapshotInSlot(SlotType, Snapshot) ? Snapshot.EquippedActor.Get() : nullptr;
}

bool URPGEquipComponent::FindEquippedWeapon(const EWeaponHandType HandType,
	URPGItemBase*& OutItem, AActor*& OutSpawnedActor, EEquipmentSlotType& OutSlotType) const
{
	OutItem = nullptr;
	OutSpawnedActor = nullptr;
	OutSlotType = EEquipmentSlotType::None;
	const auto* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	FRPGEquipmentSnapshot Snapshot;
	if (!Native || !Native->FindWeaponSnapshot(HandType, Snapshot)) return false;
	OutSpawnedActor = Snapshot.EquippedActor;
	OutSlotType = Snapshot.SlotType;
	return true; // Presence is retained while the native actor is loading; OutItem is null.
}

EEquipState URPGEquipComponent::GetCurrentEquipState() const
{
	const auto* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	return Native ? Native->GetActiveEquipState() : EEquipState::None;
}

const TArray<EEquipmentSlotType>& URPGEquipComponent::GetSlotsForState(const EEquipState State)
{
	static const TArray<EEquipmentSlotType> Primary = { EEquipmentSlotType::Weapon_Primary_L, EEquipmentSlotType::Weapon_Primary_R };
	static const TArray<EEquipmentSlotType> Secondary = { EEquipmentSlotType::Weapon_Secondary_L, EEquipmentSlotType::Weapon_Secondary_R };
	static const TArray<EEquipmentSlotType> Utility = { EEquipmentSlotType::Utility_1, EEquipmentSlotType::Utility_2 };
	static const TArray<EEquipmentSlotType> Empty;
	switch (State)
	{
	case EEquipState::WeaponSet_Primary: return Primary;
	case EEquipState::WeaponSet_Secondary: return Secondary;
	case EEquipState::Utility: return Utility;
	default: return Empty;
	}
}
