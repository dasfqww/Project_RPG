#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Skill/RPGSkillDefinition.h"
#include "UI/Skill/RPGSkillRefinementBoard.h"
#include "UI/Skill/RPGSkillRefinementButton.h"
#include "UObject/CoreRedirects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillRefinementRedirectTest,
	"ProjectRPG.Skill.RefinementMigration.ReflectionRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRefinementRedirectTest::RunTest(const FString&)
{
	// Check the names stored by pre-rename assets, including the owner already
	// redirected by the linker before a property or Blueprint event is resolved.
	const struct { ECoreRedirectFlags Type; const TCHAR* Old; const TCHAR* New; } Cases[] = {
		{ECoreRedirectFlags::Type_Class, TEXT("RPGTripodBoard"), TEXT("RPGSkillRefinementBoard")},
		{ECoreRedirectFlags::Type_Class, TEXT("RPGTripodButton"), TEXT("RPGSkillRefinementButton")},
		{ECoreRedirectFlags::Type_Struct, TEXT("RPGSkillTripodTier"), TEXT("RPGSkillRefinementTier")},
		{ECoreRedirectFlags::Type_Struct, TEXT("RPGSkillTripodOption"), TEXT("RPGSkillRefinementOption")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillSaveData.SelectedTripodIndices"), TEXT("RPGSkillSaveData.SelectedRefinementIndices")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillTripodOption.TripodTag"), TEXT("RPGSkillRefinementOption.RefinementTag")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillRefinementOption.TripodTag"), TEXT("RPGSkillRefinementOption.RefinementTag")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillDefinition.TripodTiers"), TEXT("RPGSkillDefinition.RefinementTiers")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillRuntimeSpec.SelectedTripodIndices"), TEXT("RPGSkillRuntimeSpec.SelectedRefinementIndices")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillRuntimeSpec.TripodTags"), TEXT("RPGSkillRuntimeSpec.RefinementTags")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillSlotViewModel.CurrentTripodIndices"), TEXT("RPGSkillSlotViewModel.CurrentRefinementIndices")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGTripodButton.TripodOption"), TEXT("RPGSkillRefinementButton.RefinementOption")},
		{ECoreRedirectFlags::Type_Property, TEXT("RPGSkillRefinementButton.TripodOption"), TEXT("RPGSkillRefinementButton.RefinementOption")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGPlayerSkillComponent.SelectTripod"), TEXT("RPGPlayerSkillComponent.SelectRefinement")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGPlayerSkillComponent.ServerSelectTripod"), TEXT("RPGPlayerSkillComponent.ServerSelectRefinement")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGSkillSlotViewModel.RequestTripodSelection"), TEXT("RPGSkillSlotViewModel.RequestRefinementSelection")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGSkillSlotViewModel.IsTripodSelected"), TEXT("RPGSkillSlotViewModel.IsRefinementSelected")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGTripodButton.BP_OnTripodInitialized"), TEXT("RPGSkillRefinementButton.BP_OnRefinementInitialized")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGSkillRefinementButton.BP_OnTripodInitialized"), TEXT("RPGSkillRefinementButton.BP_OnRefinementInitialized")},
		{ECoreRedirectFlags::Type_Function, TEXT("RPGCharacterConfigurationWidget.SetTripodChoice"), TEXT("RPGCharacterConfigurationWidget.SetRefinementChoice")},
	};
	for (const auto& Entry : Cases)
	{
		const FString OldPath = FString(TEXT("/Script/Project_RPG.")) + Entry.Old;
		const FString NewPath = FString(TEXT("/Script/Project_RPG.")) + Entry.New;
		TestEqual(*OldPath, FCoreRedirects::GetRedirectedName(Entry.Type,
			FCoreRedirectObjectName(OldPath)).ToString(), FCoreRedirectObjectName(NewPath).ToString());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillRefinementAuthoredMigrationTest,
	"ProjectRPG.Skill.RefinementMigration.ExistingAssetsAndStableChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRefinementAuthoredMigrationTest::RunTest(const FString&)
{
	TestNotNull(TEXT("Existing board Blueprint loads against the new native parent"),
		LoadClass<URPGSkillRefinementBoard>(nullptr,
			TEXT("/Game/Blueprints/UI/Skill/WBP_TripodBoard.WBP_TripodBoard_C")));
	TestNotNull(TEXT("Existing effect button Blueprint loads against the new native parent"),
		LoadClass<URPGSkillRefinementButton>(nullptr,
			TEXT("/Game/Blueprints/UI/Skill/WBP_TripodButton.WBP_TripodButton_C")));
	const struct { const TCHAR* Asset; const TCHAR* Choices[4]; } Skills[] = {
		{TEXT("DA_Skill_AssultBlade"), {TEXT("LongReach"), TEXT("QuickApproach"), TEXT("ExtraReach"), TEXT("ExtraRecovery")}},
		{TEXT("DA_Skill_ChargeSlash"), {TEXT("FullCharge"), TEXT("QuickCharge"), TEXT("Stagger"), TEXT("PartBreak")}},
		{TEXT("DA_Skill_WhirlWind"), {TEXT("Mobile"), TEXT("Stationary"), TEXT("WideArea"), TEXT("FinalStagger")}},
		{TEXT("DA_Skill_JumpSmash"), {TEXT("WideArea"), TEXT("PartBreak"), TEXT("ExtraArea"), TEXT("Stagger")}},
	};
	for (const auto& Entry : Skills)
	{
		const FString AssetPath = FString::Printf(TEXT("/Game/Blueprints/GameData/Skill/%s.%s"), Entry.Asset, Entry.Asset);
		const auto* Definition = LoadObject<URPGSkillDefinition>(nullptr, *AssetPath);
		if (!TestNotNull(*AssetPath, Definition)) continue;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 RequiredLevel = Index < 2 ? 4 : 7;
			int32 Tier = INDEX_NONE, Option = INDEX_NONE;
			if (!TestTrue(Entry.Choices[Index], Definition->ResolveSavedRefinement(
				RequiredLevel, FName(Entry.Choices[Index]), Tier, Option))) continue;
			TestEqual(TEXT("Authored option identity survives struct/property redirection"),
				Definition->RefinementTiers[Tier].Options[Option].PersistenceId, FName(Entry.Choices[Index]));
			FRPGSkillSaveData Saved;
			Saved.SkillLevel = RequiredLevel;
			Saved.SelectedRefinementIndices.Init(INDEX_NONE, Definition->RefinementTiers.Num());
			Saved.SelectedRefinementIndices[Tier] = Option;
			FRPGSkillRuntimeSpec Runtime;
			Definition->BuildRuntimeSpec(nullptr, Saved, Runtime);
			TestEqual(TEXT("Saved selection reaches the same runtime tier"), Runtime.SelectedRefinementIndices[Tier], Option);
		}
	}
	return true;
}

#endif
