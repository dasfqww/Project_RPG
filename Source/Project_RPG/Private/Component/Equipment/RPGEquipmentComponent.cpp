#include "Component/Equipment/RPGEquipmentComponent.h"

#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Equipment/RPGEquipComponent.h"

// Reflected legacy list types remain loadable, but no component owns/replicates them.
void FRPGEquipmentList::PostReplicatedAdd(const TArrayView<int32>, int32) {}
void FRPGEquipmentList::PostReplicatedChange(const TArrayView<int32>, int32) {}
void FRPGEquipmentList::PostReplicatedRemove(const TArrayView<int32>, int32) {}

URPGEquipmentComponent::URPGEquipmentComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

int32 URPGEquipmentComponent::CanAddEquipment(URPGItemBase*, EEquipmentSlotType) const
{
	return 0; // Native planners, not legacy quantities/Manifest, own validation.
}

void URPGEquipmentComponent::AddEquipment_Internal(EEquipmentSlotType, URPGItemBase*, int32)
{
	UE_LOG(LogTemp, Warning, TEXT("Raw legacy equipment writes are retired; use ServerEquipItem."));
}

URPGItemBase* URPGEquipmentComponent::RemoveEquipment_Internal(EEquipmentSlotType)
{
	UE_LOG(LogTemp, Warning, TEXT("Raw legacy equipment writes are retired; use ServerUnequipItem."));
	return nullptr;
}

bool URPGEquipmentComponent::IsWeaponSlot(const EEquipmentSlotType SlotType)
{
	return FRPGEquipmentSnapshot::IsWeaponSlot(SlotType);
}

bool URPGEquipmentComponent::IsArmorSlot(const EEquipmentSlotType SlotType)
{
	return SlotType >= EEquipmentSlotType::Head && SlotType <= EEquipmentSlotType::Hands;
}

bool URPGEquipmentComponent::IsUtilitySlot(const EEquipmentSlotType SlotType)
{
	return FRPGEquipmentSnapshot::IsUtilitySlot(SlotType);
}

bool URPGEquipmentComponent::IsPrimaryWeaponSlot(const EEquipmentSlotType SlotType)
{
	return SlotType == EEquipmentSlotType::Weapon_Primary_L || SlotType == EEquipmentSlotType::Weapon_Primary_R;
}

bool URPGEquipmentComponent::IsSecondaryWeaponSlot(const EEquipmentSlotType SlotType)
{
	return SlotType == EEquipmentSlotType::Weapon_Secondary_L || SlotType == EEquipmentSlotType::Weapon_Secondary_R;
}

URPGItemBase* URPGEquipmentComponent::GetItemInSlot(EEquipmentSlotType) const
{
	return nullptr;
}

int32 URPGEquipmentComponent::GetItemCountInSlot(const EEquipmentSlotType SlotType) const
{
	const auto* Native = GetOwner()
		? GetOwner()->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	FRPGEquipmentSnapshot Snapshot;
	return Native && Native->FindSnapshotInSlot(SlotType, Snapshot) ? 1 : 0;
}

const TArray<FRPGEquipmentEntry>& URPGEquipmentComponent::GetAllEntries() const
{
	static const TArray<FRPGEquipmentEntry> Empty;
	return Empty;
}

URPGEquipComponent* URPGEquipmentComponent::GetEquipComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPGEquipComponent>() : nullptr;
}
