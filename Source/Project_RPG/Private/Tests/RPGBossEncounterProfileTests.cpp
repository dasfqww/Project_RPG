#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGBossAbilityTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Ability/RPGBossSummonAbility.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossEncounterProfiles.h"
#include "AI/RPGBossPatternExecutor.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameMode/RPGGameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProfileFirst, "Tests.RaidBoss.Profile.First");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ProfileSecond, "Tests.RaidBoss.Profile.Second");

namespace BossProfileTest
{
	FRPGBossPatternDefinition Entry(FGameplayTag Id, TSubclassOf<URPGBossGameplayAbility> Class,
		ERPGBossTelegraphPolicy Policy = ERPGBossTelegraphPolicy::Inherit)
	{
		FRPGBossPatternDefinition Result; Result.PatternId = Id; Result.AbilityClass = Class; Result.TelegraphPolicy = Policy;
		return Result;
	}
	struct FWorld
	{
		UWorld* World;
		ARPGBossTestPawn* Boss;
		ARPGBossTestPawn* Target;
		ARPGAIController* Controller;
		URPGBossThreatComponent* Threat;
		uint64 Frame = GFrameCounter;
		explicit FWorld(bool bMode = false)
		{
			UWorld::InitializationValues Init; Init.EnableTraceCollision(true);
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()),
				GetTransientPackage(), true, ERHIFeatureLevel::Num, &Init);
			auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game); Context.SetCurrentWorld(World);
			FURL Url;
			if (bMode)
			{
				auto* Instance = NewObject<UGameInstance>(); Context.OwningGameInstance = Instance; World->SetGameInstance(Instance);
				Url.AddOption(TEXT("game=/Script/Project_RPG.RPGBossProfileTestGameMode")); World->SetGameMode(Url);
			}
			World->InitializeActorsForPlay(Url); World->CreateAISystem(); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			Boss = World->SpawnActor<ARPGBossTestPawn>(); Target = World->SpawnActor<ARPGBossTestPawn>();
			Controller = World->SpawnActor<ARPGAIController>(); Controller->Possess(Boss);
			Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
			auto* TargetAI = World->SpawnActor<AAIController>(); TargetAI->SetGenericTeamId(FGenericTeamId(0)); TargetAI->Possess(Target);
			Target->SetActorLocation(FVector(100.f, 0.f, 0.f));
			Threat = NewObject<URPGBossThreatComponent>(Boss); Threat->RegisterComponent();
			Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
		}
		~FWorld() { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		void TargetBoss() { Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId); Threat->RefreshTargets(); }
		void Tick(float Seconds)
		{
			for (int32 I = 0; I < FMath::CeilToInt(Seconds / .02f); ++I)
			{
				TGuardValue<uint64> Guard(GFrameCounter, ++Frame); World->Tick(LEVELTICK_All, .02f);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossProfileAuthoringTest, "ProjectRPG.Boss.Profiles.Authoring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossProfileAuthoringTest::RunTest(const FString&)
{
	using namespace BossProfileTest;
	auto* Definition = ARPGBossTestPawn::MakeDefinition(GetTransientPackage());
	auto* Profiles = NewObject<URPGBossEncounterProfiles>();
	Profiles->Profiles.Add(ERPGGameDifficulty::Hard, Definition);
	FText Error;
	TestTrue(TEXT("Legacy encounter remains a valid profile"), Profiles->ValidateProfiles(Error));
	TestTrue(TEXT("Exact difficulty mapping"), Profiles->Resolve(ERPGGameDifficulty::Hard) == Definition);
	TestNull(TEXT("Unmapped difficulty cannot silently use another difficulty"), Profiles->Resolve(ERPGGameDifficulty::Normal));
	Profiles->Fallback = Definition;
	TestTrue(TEXT("Explicit fallback is supported"), Profiles->Resolve(ERPGGameDifficulty::Normal) == Definition);
	Profiles->Profiles.Add(ERPGGameDifficulty::Normal, nullptr);
	TestFalse(TEXT("Null explicit mapping is invalid even with fallback"), Profiles->ValidateProfiles(Error));
	Profiles->Profiles.Remove(ERPGGameDifficulty::Normal);
	auto& Phase = Definition->Phases[0];
	Phase.Patterns = {Entry(TAG_ProfileFirst, URPGBossTestAbility::StaticClass()), Entry(TAG_ProfileSecond, URPGBossTestAbility::StaticClass())};
	TestTrue(TEXT("One GA can implement distinct entries"), Definition->ValidateDefinition(Error));
	Phase.Patterns[1].PatternId = TAG_ProfileFirst;
	TestFalse(TEXT("Duplicate entry identity rejected"), Definition->ValidateDefinition(Error));
	Phase.Patterns[1].PatternId = TAG_ProfileSecond;
	Phase.AbilityPatterns.Add(URPGBossTestAbility::StaticClass());
	TestFalse(TEXT("Ambiguous legacy and entry lists rejected"), Definition->ValidateDefinition(Error));
	Phase.AbilityPatterns.Reset();
	Phase.Patterns[0].Settings = NewObject<URPGBossSummonPatternSettings>(Definition);
	TestFalse(TEXT("Unsupported mechanic settings rejected"), Definition->ValidateDefinition(Error));
	Phase.Patterns[0].AbilityClass = URPGBossSummonAbility::StaticClass();
	auto* Settings = CastChecked<URPGBossSummonPatternSettings>(Phase.Patterns[0].Settings);
	Settings->LocalSpawnOffsets = {FVector(200.f, 0.f, 0.f)};
	TestTrue(TEXT("Typed summon settings accepted"), Definition->ValidateDefinition(Error));
	Settings->WindupSeconds = -1.f;
	TestFalse(TEXT("Invalid typed setting rejected at authoring time"), Definition->ValidateDefinition(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossProfileRoutingTest, "ProjectRPG.Boss.Profiles.ServerRoutingAndFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossProfileRoutingTest::RunTest(const FString&)
{
	BossProfileTest::FWorld F(true);
	auto* Mode = F.World->GetAuthGameMode<ARPGGameModeBase>();
	if (!TestNotNull(TEXT("Authoritative RPG mode"), Mode)) return false;
	Mode->SetGameDifficulty(ERPGGameDifficulty::Hard);
	auto* Profiles = NewObject<URPGBossEncounterProfiles>(F.Boss);
	auto* Hard = ARPGBossTestPawn::MakeDefinition(Profiles);
	Hard->bShowTelegraphsByDefault = false; Hard->Phases.SetNum(1); Hard->Phases[0].TimeLimitSeconds = 35.f;
	Profiles->Profiles.Add(ERPGGameDifficulty::Hard, Hard);
	Profiles->Profiles.Add(ERPGGameDifficulty::Normal, F.Boss->Boss->Definition);
	F.Boss->Boss->EncounterProfiles = Profiles;
	TestTrue(TEXT("Server difficulty selects its encounter"), F.Boss->Boss->StartEncounter());
	TestFalse(TEXT("Selected profile owns presentation default"), F.Boss->Boss->ShouldShowTelegraphsByDefault());
	TestEqual(TEXT("Selected profile changes mechanic time"), F.Boss->Boss->GetActivePhaseDefinition()->TimeLimitSeconds, 35.f);
	Hard->Phases[0].TimeLimitSeconds = 1.f; Hard->bShowTelegraphsByDefault = true;
	Mode->SetGameDifficulty(ERPGGameDifficulty::Normal);
	TestFalse(TEXT("Live mode or asset changes cannot alter active profile"), F.Boss->Boss->ShouldShowTelegraphsByDefault());
	TestEqual(TEXT("Frozen time remains unchanged"), F.Boss->Boss->GetActivePhaseDefinition()->TimeLimitSeconds, 35.f);
	F.Boss->Boss->BeginReset(); F.Boss->Boss->CompleteReset();
	TestTrue(TEXT("Next encounter selects the new server configuration"), F.Boss->Boss->StartEncounter());
	TestTrue(TEXT("Normal default restored"), F.Boss->Boss->ShouldShowTelegraphsByDefault());
	F.Boss->Boss->BeginReset(); F.Boss->Boss->CompleteReset();
	Mode->SetGameDifficulty(ERPGGameDifficulty::Hell);
	TestFalse(TEXT("Unmapped confirmed difficulty cannot start"), F.Boss->Boss->StartEncounter());
	F.Boss->Boss->EncounterProfiles = nullptr;
	TestTrue(TEXT("Removing optional routing preserves legacy definition"), F.Boss->Boss->StartEncounter());
	F.Boss->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot select or start an encounter"), F.Boss->Boss->StartEncounter());
	F.Boss->SetTestRole(ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossProfileSequenceTest, "ProjectRPG.Boss.Profiles.PatternIdentityAndSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossProfileSequenceTest::RunTest(const FString&)
{
	using namespace BossProfileTest;
	FWorld F;
	auto* Probe = NewObject<URPGBossAbilityTestProbe>(F.Boss);
	const auto Handle = F.Boss->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTestAbility::StaticClass(), 1, INDEX_NONE, Probe));
	auto* Ability = CastChecked<URPGBossTestAbility>(F.Boss->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	auto& Phase = F.Boss->Boss->Definition->Phases[0];
	Phase.Patterns = {Entry(TAG_ProfileFirst, URPGBossTestAbility::StaticClass()), Entry(TAG_ProfileSecond, URPGBossTestAbility::StaticClass())};
	Phase.PatternSelection = ERPGBossPatternSelection::Sequence;
	TestTrue(TEXT("Entry encounter starts"), F.Boss->Boss->StartEncounter()); F.TargetBoss();
	auto* Executor = F.Boss->Boss->GetPatternExecutor();
	FGameplayTag Selected;
	auto Select = [&]() { return Executor->Select(Selected); };
	auto Activate = [&]() { return Executor->TryActivate(Selected, F.Boss->Boss->GetBossState().Revision); };
	TestTrue(TEXT("First sequence entry selected"), Select() == Handle && Selected == TAG_ProfileFirst);
	TestFalse(TEXT("Named caller cannot skip sequence"), Executor->TryActivate(TAG_ProfileSecond, F.Boss->Boss->GetBossState().Revision));
	TestTrue(TEXT("First entry starts"), Activate());
	TestEqual(TEXT("GA captures entry identity"), Ability->GetBossPatternId(), TAG_ProfileFirst.GetTag());
	TestFalse(TEXT("Concurrent selection is rejected"), Select().IsValid());
	Ability->FinishTest(true);
	TestTrue(TEXT("Cancellation retries same entry"), Select().IsValid() && Selected == TAG_ProfileFirst);
	TestTrue(TEXT("Retry starts"), Activate()); Ability->FinishTest(false);
	TestTrue(TEXT("Success advances to distinct entry sharing same GA"), Select().IsValid() && Selected == TAG_ProfileSecond);
	Probe->bBlockActivation = true;
	TestFalse(TEXT("Ineligible sequence entry waits without skipping"), Select().IsValid());
	Probe->bBlockActivation = false;
	Select(); TestTrue(TEXT("Second entry starts"), Activate());
	TestEqual(TEXT("Same instance captures second identity"), Ability->GetBossPatternId(), TAG_ProfileSecond.GetTag());
	Ability->FinishTest(false);
	TestTrue(TEXT("Sequence loops only after success"), Select().IsValid() && Selected == TAG_ProfileFirst);
	F.Boss->ASC->TryActivateAbility(Handle, false);
	TestFalse(TEXT("Class-only activation cannot bypass entry policy"), Ability->IsActive());
	Probe->bEndImmediately = true;
	auto* Result = NewObject<URPGBossAbilityTestProbe>(F.Boss);
	auto* BT = StartBossAbilityTestTree(F.Controller, Result, 10.f, URPGBossTestAbility::StaticClass(), true);
	if (TestNotNull(TEXT("Production BT task"), BT)) { F.Tick(.2f); TestTrue(TEXT("BT executes an entry through GAS"), Result->LastResult == EBTNodeResult::Succeeded); }
	TestTrue(TEXT("Synchronous completion advances exactly once"), Select().IsValid() && Selected == TAG_ProfileSecond);
	F.Boss->Boss->BeginReset(); F.Boss->Boss->CompleteReset(); F.Boss->Boss->StartEncounter(); F.TargetBoss();
	TestTrue(TEXT("Reset starts sequence from entry zero"), Select().IsValid() && Selected == TAG_ProfileFirst);
	Probe->bResetOnCanActivate = true;
	TestFalse(TEXT("Authored query reset invalidates selection"), Select().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossProfileHiddenDamageTest, "ProjectRPG.Boss.Profiles.HiddenCastStillHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossProfileHiddenDamageTest::RunTest(const FString&)
{
	using namespace BossProfileTest;
	FWorld F;
	F.Boss->Boss->Definition->bShowTelegraphsByDefault = false;
	F.Boss->Boss->Definition->Phases[0].Patterns = {
		Entry(TAG_ProfileFirst, URPGBossTelegraphedStrikeAbility::StaticClass()),
		Entry(TAG_ProfileSecond, URPGBossTelegraphedStrikeAbility::StaticClass(), ERPGBossTelegraphPolicy::Show)};
	const auto Handle = F.Boss->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTelegraphedStrikeAbility::StaticClass(), 1));
	auto* Attack = CastChecked<URPGBossTelegraphedStrikeAbility>(F.Boss->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Attack->ActionTag = RPGGameplayTags::NPC_Ability_MeleeAttack; Attack->WindupSeconds = .2f; Attack->Radius = 250.f;
	Attack->DamageEffectClass = URPGBossTestDamageEffect::StaticClass(); Attack->BaseDamage.Value = 31.f;
	F.Boss->Boss->StartEncounter(); F.TargetBoss();
	const auto Context = F.Boss->Boss->GetBossState();
	TestTrue(TEXT("Hidden entry activates"), F.Boss->Boss->TryActivatePattern(TAG_ProfileFirst, Context.EncounterId, Context.PhaseTag));
	const auto Hidden = F.Boss->Boss->GetTelegraphState();
	TestTrue(TEXT("Hidden cast still publishes an active, timed snapshot"), Hidden.bActive && !Hidden.bShowIndicator && Hidden.EndServerTime > Hidden.StartServerTime);
	TestEqual(TEXT("Snapshot identifies pattern separately from style"), Hidden.PatternId, TAG_ProfileFirst.GetTag());
	float Progress = 0.f;
	TestFalse(TEXT("Late observer cannot reveal hidden range"), URPGBossTelegraphPresentationComponent::EvaluateProgress(Context, Hidden, Hidden.StartServerTime + .1, Progress));
	TestEqual(TEXT("No damage before windup finishes"), F.Target->Attributes->GetCurrentHealth(), 100.f);
	F.Boss->Boss->Definition->bShowTelegraphsByDefault = true;
	F.Tick(.8f);
	TestEqual(TEXT("Hidden attack applies normal server GAS damage"), F.Target->Attributes->GetCurrentHealth(), 69.f);
	TestTrue(TEXT("Explicit Show overrides hidden profile default"), F.Boss->Boss->TryActivatePattern(TAG_ProfileSecond, Context.EncounterId, Context.PhaseTag));
	const auto Shown = F.Boss->Boss->GetTelegraphState();
	TestTrue(TEXT("Same GA publishes visible entry override"), Shown.bActive && Shown.bShowIndicator && Shown.PatternId == TAG_ProfileSecond);
	TestTrue(TEXT("Late observer reconstructs shown range"), URPGBossTelegraphPresentationComponent::EvaluateProgress(Context, Shown, Shown.StartServerTime + .1, Progress));
	auto NewPhase = Context; ++NewPhase.PhaseIndex;
	TestFalse(TEXT("Out-of-order old phase snapshot cannot reveal its indicator"), URPGBossTelegraphPresentationComponent::EvaluateProgress(NewPhase, Shown, Shown.StartServerTime + .1, Progress));
	F.Boss->Boss->BeginReset(); F.Tick(.4f);
	TestEqual(TEXT("Reset cancels delayed impact"), F.Target->Attributes->GetCurrentHealth(), 69.f);
	TestFalse(TEXT("Reset clears range state"), F.Boss->Boss->GetTelegraphState().bActive);
	return true;
}
#endif
