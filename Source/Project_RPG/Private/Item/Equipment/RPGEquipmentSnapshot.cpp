#include "Item/Equipment/RPGEquipmentSnapshot.h"

#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Persistence/RPGItemRecord.h"

bool FRPGEquipmentSnapshot::IsWeaponSlot(const EEquipmentSlotType Slot)
{
	return Slot >= EEquipmentSlotType::Weapon_Primary_L
		&& Slot <= EEquipmentSlotType::Weapon_Secondary_R;
}

bool FRPGEquipmentSnapshot::IsUtilitySlot(const EEquipmentSlotType Slot)
{
	return Slot == EEquipmentSlotType::Utility_1 || Slot == EEquipmentSlotType::Utility_2;
}

bool FRPGEquipmentSnapshot::TryBuild(const FRPGItemRecord& Record,
	const URPGItemDefinition& Definition, FRPGEquipmentSnapshot& OutSnapshot)
{
	OutSnapshot = {};
	const URPGItemEquipmentDefinitionFragment* Fragment =
		Definition.FindFragmentByClass<URPGItemEquipmentDefinitionFragment>();
	const EEquipmentSlotType Slot = static_cast<EEquipmentSlotType>(Record.GetLocation().SlotIndex);
	if (!Record.IsStructurallyValid() || !Record.IsActive() || Record.GetQuantity() != 1
		|| Record.GetLocation().ContainerType != ERPGItemContainerType::Equipment
		|| Definition.ItemCategory != EItemCategory::Equip
		|| Record.GetDefinitionId() != Definition.GetPrimaryAssetId()
		|| Record.GetDefinitionVersion() != Definition.GetDefinitionVersion()
		|| Record.GetLocation().SlotIndex < 0
		|| Record.GetLocation().SlotIndex >= static_cast<int32>(EEquipmentSlotType::Count)
		|| !Fragment || !Fragment->CanEquipInSlot(Slot) || !Fragment->HasValidIdentityForSlot(Slot))
	{
		return false;
	}
	OutSnapshot.ItemId = Record.GetItemId();
	OutSnapshot.DefinitionId = Record.GetDefinitionId();
	OutSnapshot.DefinitionVersion = Record.GetDefinitionVersion();
	OutSnapshot.Revision = Record.GetRevision();
	OutSnapshot.SlotType = Slot;
	OutSnapshot.WeaponType = Fragment->WeaponType;
	OutSnapshot.WeaponHandType = Fragment->WeaponHandType;
	OutSnapshot.UtilityType = Fragment->UtilityType;
	OutSnapshot.StatValues = Record.GetState().GetStatValues(); // Persisted rolls, never reroll.
	OutSnapshot.EquipmentProgression = Record.GetState().GetEquipmentProgression();
	return true;
}

float FRPGEquipmentSnapshot::GetStatValue(const FGameplayTag Tag) const
{
	if (!Tag.IsValid()) return 0.f;
	for (const FRPGItemStatValue& Stat : StatValues)
	{
		if (Stat.StatTag == Tag) return Stat.Value;
	}
	return 0.f;
}
