#include "Item/Equipment/RPGEquipmentProgressionRules.h"

bool RPGEquipmentProgression::TryCalculate(const FPrimaryAssetId& Definition, const int32 Version,
	const EEquipmentSlotType Slot, const FRPGEquipmentProgressionState& Growth,
	FRPGEquipmentProgressionContribution& Out)
{
	Out = {};
	if (!Growth.IsSet()) return true;
	if (!Growth.IsValid() || Definition.PrimaryAssetType != FPrimaryAssetType(TEXT("RPGItemDefinition"))
		|| Version != 1) return false;
	struct FRecipe { const TCHAR* Name; EEquipmentSlotType Slot; float Attack; float Health; float AttackStep; float HealthStep; };
	static const FRecipe Recipes[] = {
		{TEXT("GameItem.Equipment.Progression.Starter.GreatSword"), EEquipmentSlotType::Weapon_Primary_R, 0, 0, 3, 0},
		{TEXT("GameItem.Equipment.Progression.Starter.Chest"), EEquipmentSlotType::Chest, 0, 0, 0, 40},
		{TEXT("GameItem.Equipment.Progression.Starter.Gloves"), EEquipmentSlotType::Hands, 0, 0, 2, 0},
		{TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"), EEquipmentSlotType::Weapon_Primary_R, 10, 0, 3, 0},
		{TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"), EEquipmentSlotType::Weapon_Primary_R, 10, 0, 3, 0},
		{TEXT("GameItem.Equipment.Progression.Target.Chest"), EEquipmentSlotType::Chest, 0, 100, 0, 40},
		{TEXT("GameItem.Equipment.Progression.Target.Gloves"), EEquipmentSlotType::Hands, 4, 0, 2, 0}
	};
	for (const FRecipe& Recipe : Recipes)
	{
		if (!Definition.PrimaryAssetName.ToString().Equals(Recipe.Name, ESearchCase::CaseSensitive)) continue;
		if (Slot != Recipe.Slot) return false;
		Out.Attack = Recipe.Attack + Recipe.AttackStep * Growth.Level;
		Out.MaximumHealth = Recipe.Health + Recipe.HealthStep * Growth.Level;
		return true;
	}
	return false;
}

bool RPGEquipmentProgression::TrySum(const TArray<FRPGEquipmentSnapshot>& Snapshots,
	const EEquipState ActiveState, FRPGEquipmentProgressionContribution& Out)
{
	Out = {};
	for (const FRPGEquipmentSnapshot& Snapshot : Snapshots)
	{
		FRPGEquipmentProgressionContribution Item;
		if (!TryCalculate(Snapshot.DefinitionId, Snapshot.DefinitionVersion, Snapshot.SlotType,
			Snapshot.EquipmentProgression, Item)) { Out = {}; return false; }
		// FirstSlice weapons occupy only the primary right slot; a stowed or
		// inactive weapon must not strengthen a secondary/unarmed attack.
		if (FRPGEquipmentSnapshot::IsWeaponSlot(Snapshot.SlotType)
			&& ActiveState != EEquipState::WeaponSet_Primary) continue;
		Out.Attack += Item.Attack;
		Out.MaximumHealth += Item.MaximumHealth;
	}
	return true;
}
