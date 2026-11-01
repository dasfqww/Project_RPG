#include "Economy/RPGDungeonRewardTypes.h"

#include "Dom/JsonObject.h"

bool FRPGDungeonItemReward::HasValidPersonalLootWeight() const
{
	return PersonalLootWeight >= 0 && PersonalLootWeight <= 10000
		&& (PersonalLootWeight == 0 || MaxStackSize > 0);
}

bool FRPGDungeonItemReward::WritePersonalLootWeight(FJsonObject& Object) const
{
	if (!HasValidPersonalLootWeight()) return false;
	if (PersonalLootWeight == 0) Object.RemoveField(TEXT("personalLootWeight"));
	else Object.SetNumberField(TEXT("personalLootWeight"), PersonalLootWeight);
	return true;
}

bool FRPGDungeonItemReward::HasValidPersonalLootPool(const TArray<FRPGDungeonItemReward>& Rewards)
{
	int32 TotalWeight = 0;
	for (const auto& Reward : Rewards)
	{
		if (!Reward.HasValidPersonalLootWeight()) return false;
		TotalWeight += Reward.PersonalLootWeight;
		if (TotalWeight > 10000) return false;
	}
	return true;
}

bool FRPGDungeonItemReward::HasValidEquipmentProgression() const
{
	if (!EquipmentProgression.IsSet()) return true;
	if (!EquipmentProgression.IsValid() || DefinitionVersion != 1 || Quantity != 1
		|| MaxStackSize != 1 || BindState != ERPGItemBindState::CharacterBound
		|| !DefinitionType.ToString().Equals(TEXT("RPGItemDefinition"), ESearchCase::CaseSensitive)) return false;

	// Exact backend FirstSliceEquipmentDefinitions identities, not tag prefix inference.
	static const TCHAR* const Names[] = {
		TEXT("GameItem.Equipment.Progression.Starter.GreatSword"),
		TEXT("GameItem.Equipment.Progression.Starter.Chest"),
		TEXT("GameItem.Equipment.Progression.Starter.Gloves"),
		TEXT("GameItem.Equipment.Progression.Target.EnduranceGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.CirculationGreatSword"),
		TEXT("GameItem.Equipment.Progression.Target.Chest"),
		TEXT("GameItem.Equipment.Progression.Target.Gloves")
	};
	const FString Name = DefinitionName.ToString();
	for (const TCHAR* Supported : Names)
		if (Name.Equals(Supported, ESearchCase::CaseSensitive)) return true;
	return false;
}

bool FRPGDungeonItemReward::WriteEquipmentProgression(FJsonObject& Object) const
{
	if (!HasValidEquipmentProgression()) return false;
	if (!EquipmentProgression.IsSet())
	{
		Object.RemoveField(TEXT("equipmentProgression"));
		return true;
	}
	TSharedRef<FJsonObject> Growth = MakeShared<FJsonObject>();
	Growth->SetStringField(TEXT("growthBandId"), EquipmentProgression.GrowthBandId);
	Growth->SetNumberField(TEXT("ruleVersion"), EquipmentProgression.RuleVersion);
	Growth->SetNumberField(TEXT("level"), EquipmentProgression.Level);
	Object.SetObjectField(TEXT("equipmentProgression"), Growth);
	return true;
}
