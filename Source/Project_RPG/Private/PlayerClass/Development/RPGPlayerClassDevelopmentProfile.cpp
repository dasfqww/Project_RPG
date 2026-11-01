#include "PlayerClass/Development/RPGPlayerClassDevelopmentProfile.h"

#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Skill/RPGSkillDefinition.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool URPGPlayerClassDevelopmentProfile::BuildRestoreData(FRPGPlayerSkillRestoreData& OutData, FText& OutError) const
{
	OutData = {};
	OutError = FText::GetEmpty();
	const auto Fail = [&OutError](const TCHAR* Message)
	{
		OutError = FText::FromString(Message);
		return false;
	};
	if (DataVersion != FRPGPlayerSkillRestoreData::CurrentVersion || TotalSP < 0 || !IsValid(ClassDefinition))
		return Fail(TEXT("Development profile version, class or SP is invalid."));
	if (!ClassDefinition->ValidateConfiguration(OutError)) return false;
	if (SkillSlots.Num() > URPGPlayerClassDefinition::MaxCombatSkills)
		return Fail(TEXT("Development profile exceeds the eight combat slots."));
	TSet<FGameplayTag> Acquired;
	FRPGPlayerSkillRestoreData Record;
	Record.ClassId = ClassDefinition->GetPrimaryAssetId();
	Record.Version = DataVersion;
	Record.TotalSP = TotalSP;
	for (const auto& Input : AcquiredSkills)
	{
		const auto* Binding = ClassDefinition->FindSkill(Input.SkillTag);
		if (!Binding || Acquired.Contains(Input.SkillTag) || Input.SaveData.SkillLevel < 1
			|| Input.SaveData.SkillLevel > Binding->Definition->MaxSkillLevel)
			return Fail(TEXT("Development acquisition records must name unique class skills with valid levels."));
		for (int32 Tier = 0; Tier < Input.SaveData.SelectedTripodIndices.Num(); ++Tier)
			if (Input.SaveData.SelectedTripodIndices[Tier] != INDEX_NONE && !Binding->Definition->IsTripodSelectionAllowed(
				Input.SaveData.SkillLevel, Tier, Input.SaveData.SelectedTripodIndices[Tier]))
				return Fail(TEXT("Development tripod selections are invalid."));
		Acquired.Add(Input.SkillTag);
		auto& Entry = Record.AcquiredSkills.AddDefaulted_GetRef();
		Entry.SkillTag = Input.SkillTag;
		Entry.SaveData = Input.SaveData;
	}
	TSet<FGameplayTag> Slotted;
	for (FGameplayTag Tag : SkillSlots)
	{
		if (!Tag.IsValid()) continue;
		if (!Acquired.Contains(Tag) || Slotted.Contains(Tag))
			return Fail(TEXT("Development slots must reference unique explicitly acquired skills."));
		Slotted.Add(Tag);
	}
	OutData = MoveTemp(Record); // The progression owner remains responsible for authoritative SP calculation.
	return true;
}

#if WITH_EDITOR
EDataValidationResult URPGPlayerClassDevelopmentProfile::IsDataValid(FDataValidationContext& Context) const
{
	const auto ParentResult = Super::IsDataValid(Context);
	FRPGPlayerSkillRestoreData Record;
	FText Error;
	if (!BuildRestoreData(Record, Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : ClassDefinition->IsDataValid(Context);
}
#endif
