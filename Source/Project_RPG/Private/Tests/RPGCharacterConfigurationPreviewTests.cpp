#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Progression/RPGCharacterProgressionSnapshot.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "UI/Lobby/RPGCharacterConfigurationPreview.h"
#include "UI/Lobby/RPGCharacterConfigurationWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConfigurationPreviewChoicesTest,
	"ProjectRPG.Online.ConfigurationPreview.AuthoredChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConfigurationPreviewChoicesTest::RunTest(const FString&)
{
	auto* Assault = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_AssultBlade.DA_Skill_AssultBlade"));
	auto* Charge = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	auto* Jump = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_JumpSmash.DA_Skill_JumpSmash"));
	auto* Whirl = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_WhirlWind.DA_Skill_WhirlWind"));
	if (!Assault || !Charge || !Jump || !Whirl) return false;
	FRPGSavedProgressionSkill Draft;
	Draft.SkillTag = Assault->SkillTag;
	Draft.Level = 7;
	for (const FString First : {FString(TEXT("LongReach")), FString(TEXT("QuickApproach"))})
		for (const FString Second : {FString(TEXT("ExtraReach")), FString(TEXT("ExtraRecovery"))})
		{
			Draft.TripodIds = {First, Second};
			const auto Before = Draft;
			FRPGSkillRuntimeSpec Preview;
			if (!TestTrue(TEXT("Current authored Assault choice resolves by stable ID"),
				RPGCharacterConfigurationPreview::Resolve(*Assault, Draft, Preview))) return false;
			TestEqual(TEXT("Preview uses effective 8/7/6/5 second cooldown"), Preview.GetCooldownDuration(),
				8.f - (First == TEXT("QuickApproach") ? 2.f : 0.f) - (Second == TEXT("ExtraRecovery") ? 1.f : 0.f));
			float Scale;
			Preview.TryGetMontageMovementScale(Scale);
			TestTrue(TEXT("Distance tradeoff accompanies the cooldown choice"), FMath::IsNearlyEqual(Scale,
				(First == TEXT("QuickApproach") ? .8f : 1.2f) * (Second == TEXT("ExtraReach") ? 1.1f : 1.f)));
			TestTrue(TEXT("Preview never normalizes or rewrites the UI draft"), Draft.TripodIds == Before.TripodIds && Draft.Level == Before.Level);
		}
	Draft.SkillTag = Charge->SkillTag;
	Draft.Level = 4;
	for (const FString Choice : {FString(TEXT("FullCharge")), FString(TEXT("QuickCharge"))})
	{
		Draft.TripodIds = {Choice, FString()};
		FRPGSkillRuntimeSpec Preview;
		if (!RPGCharacterConfigurationPreview::Resolve(*Charge, Draft, Preview)) return false;
		const auto* Config = Preview.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
		if (!TestNotNull(TEXT("Preview retains the authored charge config"), Config)) return false;
		TestTrue(TEXT("Charge alternatives expose 1.2/0.7 seconds"), FMath::IsNearlyEqual(
			Config->ChargeTimePerLevel * Config->MaxChargeLevel, Choice == TEXT("FullCharge") ? 1.2f : .7f));
	}
	Draft.SkillTag = Jump->SkillTag;
	Draft.Level = 7;
	Draft.TripodIds = {TEXT("PartBreak"), TEXT("Stagger")};
	FRPGSkillRuntimeSpec Preview;
	if (!TestTrue(TEXT("Both Jump contribution choices resolve"), RPGCharacterConfigurationPreview::Resolve(*Jump, Draft, Preview))) return false;
	FRPGCombatContributionAmounts Amounts;
	Preview.TryGetCombatContributions(Amounts);
	TestEqual(TEXT("Stagger preview includes its selected enhancement"), Amounts.Stagger, 25.f);
	TestEqual(TEXT("Part-break preview includes its selected enhancement"), Amounts.PartBreak, 25.f);
	Draft.SkillTag = Whirl->SkillTag;
	Draft.TripodIds = {TEXT("Mobile"), TEXT("FinalStagger")};
	if (!TestTrue(TEXT("Existing finisher content is read without changing it"), RPGCharacterConfigurationPreview::Resolve(*Whirl, Draft, Preview))) return false;
	TestEqual(TEXT("Finisher timing restriction is preserved in the preview"), Preview.ContributionHitSection, FName(TEXT("End")));
	TestFalse(TEXT("A valid authored choice has a readable summary"), RPGCharacterConfigurationPreview::Describe(Whirl, Draft).IsEmpty());
	TestEqual(TEXT("Preview did not alter authored base cooldown"), Assault->BaseCooldown, 8.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConfigurationPreviewInvalidTest,
	"ProjectRPG.Online.ConfigurationPreview.RejectUnknownOrLockedChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConfigurationPreviewInvalidTest::RunTest(const FString&)
{
	auto* Definition = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	if (!Definition) return false;
	FRPGSavedProgressionSkill Draft;
	Draft.SkillTag = Definition->SkillTag;
	Draft.Level = 7;
	Draft.TripodIds = {TEXT("FullCharge"), TEXT("Stagger")};
	FRPGSkillRuntimeSpec Preview;
	if (!RPGCharacterConfigurationPreview::Resolve(*Definition, Draft, Preview)) return false;
	Draft.TripodIds[1] = TEXT("UnknownChoice");
	TestFalse(TEXT("Unknown saved IDs cannot silently display base values"), RPGCharacterConfigurationPreview::Resolve(*Definition, Draft, Preview));
	TestFalse(TEXT("Failure clears previous skill identity"), Preview.SkillTag.IsValid());
	Draft.TripodIds = {TEXT("FullCharge"), FString()};
	Draft.Level = 1;
	TestFalse(TEXT("Locked choices are not normalized into misleading base previews"), RPGCharacterConfigurationPreview::Resolve(*Definition, Draft, Preview));
	Draft.TripodIds = {FString(), FString()};
	Draft.Level = 99;
	TestFalse(TEXT("Out-of-range levels are not silently clamped"), RPGCharacterConfigurationPreview::Resolve(*Definition, Draft, Preview));
	Draft.Level = 1;
	Draft.SkillTag = FGameplayTag::RequestGameplayTag(TEXT("Player.Ability.Skill.JumpSmash"));
	TestFalse(TEXT("A different definition cannot supply preview data for this row"), RPGCharacterConfigurationPreview::Resolve(*Definition, Draft, Preview));
	TestFalse(TEXT("Missing content yields a readable unavailable notice"), RPGCharacterConfigurationPreview::Describe(nullptr, Draft).IsEmpty());
	return true;
}

#endif
