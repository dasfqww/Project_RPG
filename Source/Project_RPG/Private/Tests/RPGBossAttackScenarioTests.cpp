#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossAttackScenario.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossAttackScenarioTest, "ProjectRPG.Boss.AttackScenario",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossAttackScenarioTest::RunTest(const FString& Parameters)
{
	using namespace RPGBossAttackScenarios;
	const auto& Basic = Select(TEXT("-RPGBossMapE2EPlayerAttack"));
	TestEqual(TEXT("Basic retains its identity"), FString(Basic.SkillName), FString(TEXT("DefaultAttack")));
	TestFalse(TEXT("Basic does not use skill query evidence"), Basic.IsSkill());
	TestFalse(TEXT("Basic does not require charge observations"), Basic.IsCharged());
	TestEqual(TEXT("Held combo has no single-hit assertion"), Basic.ExpectedDamageCount, 0);
	TestEqual(TEXT("Unrelated flags select basic"), FString(Select(TEXT("-unattended")).SkillName), FString(Basic.SkillName));
	TestEqual(TEXT("No selected skill"), CountSelectedSkills(TEXT("-RPGBossMapE2EPlayerAttack")), 0);
	TestEqual(TEXT("Conflicting skills counted for rejection"), CountSelectedSkills(TEXT("-RPGBossMapE2EJumpSmash -RPGBossMapE2EChargeSlash")), 2);
	TestEqual(TEXT("Flag substrings do not select skills"), CountSelectedSkills(TEXT("-RPGBossMapE2EJumpSmashExtra")), 0);
	for (const auto& Scenario : All())
	{
		TestTrue(TEXT("Every identity is complete"), Scenario.SkillName && Scenario.AbilityPath && Scenario.MontageName && Scenario.DamageEvent.IsValid());
		TestNull(TEXT("No ASC cannot resolve an ability"), FindAbility(nullptr, Scenario));
		if (!Scenario.IsSkill()) continue;
		TestEqual(TEXT("Registry resolves its own command flag"), FString(Select(*FString::Printf(TEXT("-%s"), Scenario.CommandFlag)).SkillName), FString(Scenario.SkillName));
		TestFalse(TEXT("A skill cannot use a fallback weapon input"), ResolveInputTag(nullptr, Scenario).IsValid());
		TestFalse(TEXT("Empty effect cannot prove an authored server hit"), Scenario.HitEvidence->ContainsServerHit(FGameplayEffectSpec(), nullptr));
		TestEqual(TEXT("Only repeating channel allows variable hit count"), Scenario.ExpectedDamageCount, Scenario.IsChanneling() ? 0 : 1);
	}
	const auto& Ground = Select(TEXT("-RPGBossMapE2EJumpSmash"));
	const auto& Soft = Select(TEXT("-RPGBossMapE2EAssultBlade"));
	const auto& Charged = Select(TEXT("-RPGBossMapE2EChargeSlash"));
	TestTrue(TEXT("JumpSmash uses ground aim"), Ground.bAimAtGround);
	TestFalse(TEXT("AssultBlade does not use ground aim"), Soft.bAimAtGround);
	TestTrue(TEXT("Only charged scenario needs charge observations"), Charged.IsCharged() && !Ground.IsCharged() && !Soft.IsCharged());
	TestEqual(TEXT("Charge prerequisite declared independently"), Charged.PrerequisiteTag, FGameplayTag(RPGGameplayTags::Player_Status_Rage_Active));
	TestTrue(TEXT("Charge observer hold sections declared in scenario"), Charged.ObservedHoldSections.Contains(FName(TEXT("Loop"))));
	FRPGBossAttackChargeSnapshot Snapshot;
	Snapshot.bInputPressed = true;
	TestFalse(TEXT("Null activation cannot prove charge state"), Charged.ChargeObservation->ReadCharge(nullptr, Snapshot));
	TestFalse(TEXT("Failed observation clears stale state"), Snapshot.bInputPressed);
	TestFalse(TEXT("Unrelated ability cannot satisfy charge contract"), Charged.ChargeObservation->ReadCharge(GetDefault<UGameplayAbility>(), Snapshot));
	const auto& Channel = Select(TEXT("-RPGBossMapE2EWhirlWind"));
	TestTrue(TEXT("Channel capability is separate from charge"), Channel.IsChanneling() && !Channel.IsCharged() && !Charged.IsChanneling());
	TestFalse(TEXT("Channel needs no Rage fixture"), Channel.PrerequisiteTag.IsValid());
	TestTrue(TEXT("Channel uses authored Toggle repeating section, not charge Loop"), Channel.ObservedHoldSections.Contains(FName(TEXT("Toggle"))) && !Channel.ObservedHoldSections.Contains(FName(TEXT("Loop"))));
	TestEqual(TEXT("Channel flags conflict with other skills"), CountSelectedSkills(TEXT("-RPGBossMapE2EWhirlWind -RPGBossMapE2EChargeSlash")), 2);
	FRPGBossAttackChannelSnapshot ChannelState;
	ChannelState.bInputPressed = true;
	ChannelState.LoopSeconds = 99.f;
	TestFalse(TEXT("No activation cannot prove channel"), Channel.ChannelObservation->ReadChannel(nullptr, nullptr, ChannelState));
	TestFalse(TEXT("Failed channel read clears input"), ChannelState.bInputPressed);
	TestEqual(TEXT("Failed channel read clears stale cadence"), ChannelState.LoopSeconds, 0.f);
	TestFalse(TEXT("Unrelated ability cannot satisfy channel"), Channel.ChannelObservation->ReadChannel(GetDefault<UGameplayAbility>(), nullptr, ChannelState));
	TestTrue(TEXT("Regular channel cadence accepted"), AcceptsChannelCadence(.233, .033f, .033f, .233f, 1));
	TestFalse(TEXT("Duplicate within ordinary animation update rejected"), AcceptsChannelCadence(0.0, .033f, .033f, .233f, 2));
	TestTrue(TEXT("Queued catch-up traversal accepted"), AcceptsChannelCadence(0.0, .5f, .5f, .233f, 3));
	TestFalse(TEXT("Catch-up cannot exceed traversal budget"), AcceptsChannelCadence(0.0, .5f, .5f, .233f, 4));
	TestTrue(TEXT("Late-dispatched hit does not shift the next authored notify"), AcceptsChannelCadence(.004, .263f, .033f, .233f, 1));
	TestFalse(TEXT("Burst without real catch-up rejected"), AcceptsChannelCadence(.004, .033f, .033f, .233f, 1));
	TestFalse(TEXT("Invalid channel period rejected"), AcceptsChannelCadence(.233, .033f, .033f, 0.f, 1));
	TestFalse(TEXT("Backward channel clock rejected"), AcceptsChannelCadence(-.01, .5f, .033f, .233f, 1));
	return true;
}
#endif
