#include "DataAsset/Definition/RPGGladiatorData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGladiatorData)

FSoftObjectPath URPGAssetData::GetAssetPathByName(const FName AssetName) const
{
	if (const FSoftObjectPath* FoundPath = AssetNameToPath.Find(AssetName))
	{
		return *FoundPath;
	}

	for (const TPair<FName, FRPGAssetSet>& Group : AssetGroupNameToSet)
	{
		for (const FRPGAssetEntry& Entry : Group.Value.AssetEntries)
		{
			if (Entry.AssetName == AssetName)
			{
				return Entry.AssetPath;
			}
		}
	}

	return FSoftObjectPath();
}

const FRPGAssetSet* URPGAssetData::FindAssetSetByLabel(const FName Label) const
{
	if (const FRPGAssetSet* FoundSet = AssetLabelToSet.Find(Label))
	{
		return FoundSet;
	}

	return AssetGroupNameToSet.Find(Label);
}

const FRPGDefaultArmorMeshSet* URPGCharacterData::FindDefaultArmorMeshSet(const ERPGGladiatorSkinType SkinType) const
{
	return DefaultArmorMeshMap.Find(SkinType);
}

bool URPGElectricFieldPhaseData::IsValidPhaseIndex(const int32 PhaseIndex) const
{
	return PhaseEntries.IsValidIndex(PhaseIndex);
}

const FRPGElectricFieldPhaseEntry* URPGElectricFieldPhaseData::FindPhaseEntry(const int32 PhaseIndex) const
{
	return PhaseEntries.IsValidIndex(PhaseIndex) ? &PhaseEntries[PhaseIndex] : nullptr;
}
