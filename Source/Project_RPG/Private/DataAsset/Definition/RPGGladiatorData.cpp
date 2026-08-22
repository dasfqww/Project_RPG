#include "DataAsset/Definition/RPGGladiatorData.h"

#include "Ability/RPGAbilitySet.h"
#include "Component/RPGAbilitySystemComponent.h"

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

const URPGClassData* URPGClassData::GetDefaultClassData()
{
	return LoadObject<URPGClassData>(
		nullptr,
		TEXT("/GladiatorCore/Data/ClassData_GladiatorGame.ClassData_GladiatorGame"));
}

const FRPGClassInfoEntry* URPGClassData::FindClassInfo(const ERPGGladiatorCharacterClass CharacterClass) const
{
	const int32 ClassIndex = static_cast<int32>(CharacterClass);
	return ClassIndex >= 0 && ClassIndex < static_cast<int32>(ERPGGladiatorCharacterClass::Count)
		? &ClassInfoEntries[ClassIndex]
		: nullptr;
}

bool URPGClassData::GiveClassAbilitiesToAbilitySystem(
	const ERPGGladiatorCharacterClass CharacterClass,
	URPGAbilitySystemComponent* AbilitySystemComponent,
	FRPGAbilitySet_GrantedHandles* OutGrantedHandles,
	UObject* SourceObject) const
{
	const FRPGClassInfoEntry* ClassInfo = FindClassInfo(CharacterClass);
	if (!ClassInfo || !IsValid(ClassInfo->ClassAbilitySet) || !IsValid(AbilitySystemComponent))
	{
		return false;
	}

	ClassInfo->ClassAbilitySet->GiveToAbilitySystem(
		AbilitySystemComponent,
		OutGrantedHandles,
		SourceObject ? SourceObject : AbilitySystemComponent->GetAvatarActor());
	return AbilitySystemComponent->IsOwnerActorAuthoritative();
}

bool URPGElectricFieldPhaseData::IsValidPhaseIndex(const int32 PhaseIndex) const
{
	return PhaseEntries.IsValidIndex(PhaseIndex);
}

const FRPGElectricFieldPhaseEntry* URPGElectricFieldPhaseData::FindPhaseEntry(const int32 PhaseIndex) const
{
	return PhaseEntries.IsValidIndex(PhaseIndex) ? &PhaseEntries[PhaseIndex] : nullptr;
}
