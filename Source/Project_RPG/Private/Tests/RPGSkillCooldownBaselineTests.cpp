#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "DataTable/SkillData.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillCooldownReductionTest,
	"ProjectRPG.Skill.Cooldown.FlatReductionAndFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillCooldownReductionTest::RunTest(const FString& Parameters)
{
	auto* Definition = NewObject<URPGSkillDefinition>();
	Definition->BaseCooldown = 8.f;
	for (int32 Level : {4, 7})
	{
		auto& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
		Tier.RequiredSkillLevel = Level;
		Tier.Options.AddDefaulted_GetRef().CooldownReductionSeconds = Level == 4 ? 2.f : 1.f;
	}
	FRPGSkillSaveData Save;
	Save.SelectedTripodIndices = {0, 0};
	FRPGSkillRuntimeSpec Runtime;
	Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
	TestEqual(TEXT("Locked tiers provide no reduction"), Runtime.GetCooldownDuration(), 8.f);
	Save.SkillLevel = 4;
	Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
	TestEqual(TEXT("Only the unlocked selected tier contributes"), Runtime.GetCooldownDuration(), 6.f);
	Save.SkillLevel = 7;
	Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
	TestEqual(TEXT("Flat seconds sum instead of multiplying ratios"), Runtime.GetCooldownDuration(), 5.f);
	const auto ScalarTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Cooldown"));
	Runtime.StatScalars.Add(ScalarTag, 0.5f);
	TestEqual(TEXT("Flat reduction precedes the existing scalar"), Runtime.GetCooldownDuration(), 2.5f);
	Definition->TripodTiers[0].Options[0].CooldownReductionSeconds = 30.f;
	TestEqual(TEXT("Definition edits cannot change a built activation"), Runtime.GetCooldownDuration(), 2.5f);
	Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
	TestEqual(TEXT("Excessive reductions retain the minimum repeat guard"), Runtime.GetCooldownDuration(), 1.f);
	Definition->TripodTiers[0].Options[0].CooldownReductionSeconds = -2.f;
	Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
	TestEqual(TEXT("Malformed negative data gives no reduction"), Runtime.GetCooldownDuration(), 7.f);
	Runtime.CooldownReductionSeconds = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("Invalid frozen reduction gives no benefit"), Runtime.GetCooldownDuration(), 8.f);
	Runtime.Reset();
	TestEqual(TEXT("Reused snapshots reset the reduction"), Runtime.CooldownReductionSeconds, 0.f);
#if WITH_EDITOR
	auto* Authored = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade.DA_Skill_AssultBlade"));
	if (!TestNotNull(TEXT("Validation fixture"), Authored)) return false;
	FDataValidationContext Valid;
	TestEqual(TEXT("Authored flat reductions pass editor validation"), Authored->IsDataValid(Valid), EDataValidationResult::Valid);
	auto* Invalid = DuplicateObject<URPGSkillDefinition>(Authored, GetTransientPackage());
	Invalid->TripodTiers[0].Options[0].CooldownReductionSeconds = -1.f;
	FDataValidationContext Negative;
	TestEqual(TEXT("Editor validation rejects negative seconds"), Invalid->IsDataValid(Negative), EDataValidationResult::Invalid);
	Invalid->TripodTiers[0].Options[0].CooldownReductionSeconds = std::numeric_limits<float>::quiet_NaN();
	FDataValidationContext Nonfinite;
	TestEqual(TEXT("Editor validation rejects nonfinite seconds"), Invalid->IsDataValid(Nonfinite), EDataValidationResult::Invalid);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGGreatSwordCooldownContentTest,
	"ProjectRPG.Skill.Cooldown.GreatSwordAuthoredBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGGreatSwordCooldownContentTest::RunTest(const FString& Parameters)
{
	struct FSkill { const TCHAR* Name; const TCHAR* Ability; float Seconds; };
	const FSkill Skills[] = {
		{TEXT("AssultBlade"), TEXT("GA_PlayerSkill_AssultBlade"), 8.f},
		{TEXT("JumpSmash"), TEXT("GA_Player_JumpSmash"), 10.f},
		{TEXT("ChargeSlash"), TEXT("GA_Player_ChargeSlash"), 12.f},
		{TEXT("WhirlWind"), TEXT("GA_Player_WhirlWind"), 10.f}};
	for (const auto& Skill : Skills)
	{
		const FString Path = FString::Printf(TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_%s.DA_Skill_%s"), Skill.Name, Skill.Name);
		auto* Definition = LoadObject<URPGSkillDefinition>(nullptr, *Path);
		if (!TestNotNull(*Path, Definition)) return false;
		FRPGSkillRuntimeSpec Runtime;
		Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime);
		TestEqual(*FString::Printf(TEXT("%s authored cooldown"), Skill.Name), Runtime.GetCooldownDuration(), Skill.Seconds);
		const auto* Row = Definition->SkillDataHandle.GetRow<FRPGSkillDataTable>(TEXT("Cooldown baseline"));
		if (!TestNotNull(TEXT("Authored skill table row"), Row)) return false;
		TestEqual(TEXT("First slice uses no mana cost"), Row->ManaCost, 0.f);
		const FString AbilityPath = FString::Printf(TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/%s.%s_C"), Skill.Ability, Skill.Ability);
		auto* Class = LoadClass<URPGGameplayAbility_SkillContainer>(nullptr, *AbilityPath);
		if (!TestNotNull(*AbilityPath, Class)) return false;
		TestNull(TEXT("No separate inherited mana cost effect"), Class->GetDefaultObject<URPGGameplayAbility_SkillContainer>()->GetCostGameplayEffect());
		if (FCString::Strcmp(Skill.Name, TEXT("AssultBlade")) != 0) continue;
		for (const FName First : {FName(TEXT("LongReach")), FName(TEXT("QuickApproach"))})
			for (const FName Second : {FName(TEXT("ExtraReach")), FName(TEXT("ExtraRecovery"))})
			{
				FRPGSkillSaveData Save;
				Save.SkillLevel = 7;
				Save.SelectedTripodIndices.Init(INDEX_NONE, Definition->TripodTiers.Num());
				for (const auto& Choice : {TPair<int32, FName>(4, First), TPair<int32, FName>(7, Second)})
				{
					int32 Tier, Option;
					if (!TestTrue(TEXT("Saved choice resolves independently of its index"),
						Definition->ResolveSavedTripod(Choice.Key, Choice.Value, Tier, Option))) return false;
					Save.SelectedTripodIndices[Tier] = Option;
				}
				Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
				const float Expected = 8.f - (First == TEXT("QuickApproach") ? 2.f : 0.f) - (Second == TEXT("ExtraRecovery") ? 1.f : 0.f);
				TestEqual(TEXT("Authored recovery combinations are 8/7/6/5 seconds"), Runtime.GetCooldownDuration(), Expected);
				float Distance = 0;
				TestTrue(TEXT("Every authored combination retains bounded movement"), Runtime.TryGetMontageMovementScale(Distance));
				TestTrue(TEXT("Short approach composes with the existing extra distance"), FMath::IsNearlyEqual(Distance,
					(First == TEXT("QuickApproach") ? 0.8f : 1.2f) * (Second == TEXT("ExtraReach") ? 1.1f : 1.f)));
			}
	}
	return true;
}

#endif
