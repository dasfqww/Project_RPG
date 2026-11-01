#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossAbilityTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Ability/RPGBossPatternCooldownEffect.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossThreatComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossPhasePatternTest, "ProjectRPG.Boss.PhasePatterns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossPhasePatternTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Pattern selection world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	ARPGBossTestPawn* Target = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	AAIController* TargetController = World->SpawnActor<AAIController>();
	if (!Boss || !Target || !Controller || !TargetController) { AddError(TEXT("Pattern fixture spawn failed")); return false; }
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	TargetController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->Possess(Target);
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	URPGBossAbilityTestProbe* Result = NewObject<URPGBossAbilityTestProbe>(Boss);
	URPGBossAbilityTestProbe* First = NewObject<URPGBossAbilityTestProbe>(Boss);
	URPGBossAbilityTestProbe* Second = NewObject<URPGBossAbilityTestProbe>(Boss);
	First->bEndImmediately = Second->bEndImmediately = true;
	const FGameplayAbilitySpecHandle FirstHandle = Boss->ASC->GiveAbility(
		FGameplayAbilitySpec(URPGBossCooldownPatternTestAbility::StaticClass(), 1, INDEX_NONE, First));
	Boss->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTestAbility::StaticClass(), 1, INDEX_NONE, Second));
	Boss->Boss->Definition->Phases[0].AbilityPatterns = { URPGBossTelegraphedStrikeAbility::StaticClass(),
		URPGBossCooldownPatternTestAbility::StaticClass(), URPGBossTestAbility::StaticClass() };
	Boss->Boss->Definition->Phases[1].AbilityPatterns = { URPGBossTestAbility::StaticClass() };
	uint64 SimulatedFrame = GFrameCounter;
	auto TickWorld = [&](float DeltaSeconds)
	{
		// TimerManager ticks only once per engine frame, even in an isolated synchronous world.
		// Restore the real frame counter after each simulated tick, as RaidContent does.
		TGuardValue<uint64> FrameGuard(GFrameCounter, ++SimulatedFrame);
		World->Tick(LEVELTICK_All, DeltaSeconds);
		Threat->RefreshTargets();
	};
	auto Run = [&]()
	{
		Result->LastResult = EBTNodeResult::InProgress;
		// Explicit class intentionally differs from phase two's selected class.
		UBehaviorTreeComponent* BT = StartBossAbilityTestTree(Controller, Result, 120.f,
			URPGBossCooldownPatternTestAbility::StaticClass(), true);
		if (!BT) { AddError(TEXT("Pattern tree could not start")); return EBTNodeResult::Failed; }
		BT->SetComponentTickEnabled(false);
		for (int32 Index = 0; Index < 6; ++Index)
		{
			TickWorld(.02f);
			if (BT->IsRunning()) { BT->TickComponent(.02f, LEVELTICK_All, nullptr); }
		}
		TestFalse(TEXT("Immediate pattern tree finished"), BT->IsRunning());
		return Result->LastResult;
	};
	TestNull(TEXT("Idle exposes no active phase configuration"), Boss->Boss->GetActivePhaseDefinition());
	TestTrue(TEXT("Idle selection fails"), Run() == EBTNodeResult::Failed);
	TestTrue(TEXT("Encounter starts"), Boss->Boss->StartEncounter());
	Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId);
	TestTrue(TEXT("First eligible pattern succeeds; missing grant is skipped"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Priority selects first granted ability"), First->Activated, 1);
	TestEqual(TEXT("Lower priority is not activated"), Second->Activated, 0);
	TestEqual(TEXT("Selector never grants the missing ability"), Boss->ASC->GetActivatableAbilities().Num(), 2);
	const FGameplayTag Cooldown = FGameplayTag::RequestGameplayTag(TEXT("Tests.RaidBoss.Pattern.Cooldown"));
	Boss->ASC->AddLooseGameplayTag(Cooldown);
	TestTrue(TEXT("GAS cooldown denial selects the next pattern"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Cooldown prevents first activation"), First->Activated, 1);
	TestEqual(TEXT("Cooldown fallback activated once"), Second->Activated, 1);
	Boss->ASC->RemoveLooseGameplayTag(Cooldown);
	First->bBlockActivation = Second->bBlockActivation = true;
	TestTrue(TEXT("All GAS-denied patterns fail without implicit fallback"), Run() == EBTNodeResult::Failed);
	First->bBlockActivation = Second->bBlockActivation = false;
	First->bEndImmediately = false;
	TestTrue(TEXT("Foreign first-pattern execution starts"), Boss->ASC->TryActivateAbility(FirstHandle, false));
	TestTrue(TEXT("Already-active pattern is skipped"), Run() == EBTNodeResult::Succeeded);
	URPGBossTestAbility* Foreign = CastChecked<URPGBossTestAbility>(Boss->ASC->FindAbilitySpecFromHandle(FirstHandle)->GetPrimaryInstance());
	TestTrue(TEXT("Selector cleanup leaves foreign execution active"), Foreign->IsActive());
	Foreign->FinishTest(false);
	First->bEndImmediately = true;
	Boss->Boss->Definition->Phases[0].AbilityPatterns.Reset();
	const int32 BeforeFrozenRun = First->Activated;
	TestTrue(TEXT("Editable asset changes cannot erase frozen phase patterns"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Frozen first pattern still selected"), First->Activated, BeforeFrozenRun + 1);
	Boss->SetTestHealth(69.f);
	const int32 BeforePhaseRun = Second->Activated;
	TestTrue(TEXT("Phase two selects its data instead of explicit AbilityClass"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Phase two selects the configured second ability"), Second->Activated, BeforePhaseRun + 1);
	Boss->SetTestHealth(20.f);
	TestTrue(TEXT("Empty phase list fails, not an explicit-class fallback"), Run() == EBTNodeResult::Failed);
	Boss->Boss->BeginReset();
	TestNull(TEXT("Reset exposes no active phase"), Boss->Boss->GetActivePhaseDefinition());
	Boss->Boss->Definition->Phases[0].AbilityPatterns = { URPGBossTestAbility::StaticClass() };
	Boss->SetTestHealth(100.f);
	Boss->Boss->CompleteReset();
	Boss->Boss->StartEncounter();
	Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId);
	const int32 BeforeRestart = Second->Activated;
	TestTrue(TEXT("Restart captures updated source patterns"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Restart uses new first-phase pattern"), Second->Activated, BeforeRestart + 1);
	Boss->SetTestRole(ROLE_SimulatedProxy);
	TestNull(TEXT("Client role cannot read authoritative pattern selection data"), Boss->Boss->GetActivePhaseDefinition());
	TestTrue(TEXT("Client role cannot select or activate"), Run() == EBTNodeResult::Failed);
	Boss->SetTestRole(ROLE_Authority);
	Second->bResetOnCanActivate = true;
	const int32 BeforeQueryReset = Second->Activated;
	TestTrue(TEXT("Reset during authored activation query invalidates selection"), Run() == EBTNodeResult::Failed);
	TestEqual(TEXT("Old selection does not activate after query reset"), Second->Activated, BeforeQueryReset);
	TestTrue(TEXT("Activation query entered reset"), Boss->Boss->GetBossState().EncounterState == ERPGBossEncounterState::Resetting);
	Second->bResetOnCanActivate = false;
	TestTrue(TEXT("Query reset completes"), Boss->Boss->CompleteReset());
	TestTrue(TEXT("Query reset can restart"), Boss->Boss->StartEncounter());
	TestTrue(TEXT("Server wipes restarted encounter"), Boss->Boss->MarkWiped());
	TestNull(TEXT("Wiped boss exposes no active phase"), Boss->Boss->GetActivePhaseDefinition());
	TestTrue(TEXT("Wiped selection fails"), Run() == EBTNodeResult::Failed);

	// A real duration GE must drive fallback and expire through GAS world ticking.
	Boss->Boss->BeginReset();
	Boss->SetTestHealth(100.f);
	URPGBossAbilityTestProbe* Timed = NewObject<URPGBossAbilityTestProbe>(Boss);
	Timed->bEndImmediately = Timed->bCommitOnActivate = true;
	const FGameplayAbilitySpecHandle TimedHandle = Boss->ASC->GiveAbility(
		FGameplayAbilitySpec(URPGBossTimedCooldownPatternTestAbility::StaticClass(), 1, INDEX_NONE, Timed));
	URPGBossTimedCooldownPatternTestAbility* TimedAbility = CastChecked<URPGBossTimedCooldownPatternTestAbility>(
		Boss->ASC->FindAbilitySpecFromHandle(TimedHandle)->GetPrimaryInstance());
	Boss->Boss->Definition->Phases[0].AbilityPatterns = {
		URPGBossTimedCooldownPatternTestAbility::StaticClass(), URPGBossTestAbility::StaticClass() };
	TestTrue(TEXT("Timed cooldown reset completes"), Boss->Boss->CompleteReset());
	TestTrue(TEXT("Timed cooldown encounter starts"), Boss->Boss->StartEncounter());
	Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId);
	const FGameplayTag TimedTag = TimedAbility->PatternCooldownTag;
	TestTrue(TEXT("First timed pattern commits and finishes"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Timed priority pattern activated"), Timed->Activated, 1);
	TestTrue(TEXT("Commit applies a real granted cooldown tag"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	const FGameplayEffectQuery TimedQuery = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(TimedTag));
	TestEqual(TEXT("Exactly one timed cooldown effect exists"), Boss->ASC->GetActiveEffects(TimedQuery).Num(), 1);
	URPGBossTestAbility* FallbackAbility = CastChecked<URPGBossTestAbility>(
		Boss->ASC->FindAbilitySpecFromClass(URPGBossTestAbility::StaticClass())->GetPrimaryInstance());
	FallbackAbility->PatternCooldownSeconds = .8f;
	FallbackAbility->PatternCooldownTag = TimedTag;
	TestTrue(TEXT("Shared cooldown tag blocks both patterns"), Run() == EBTNodeResult::Failed);
	FallbackAbility->PatternCooldownSeconds = 0.f;
	FallbackAbility->PatternCooldownTag = FGameplayTag();
	const int32 BeforeTimedFallback = Second->Activated;
	TestTrue(TEXT("Duration GE drives lower priority selection"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Timed cooldown prevents reactivation"), Timed->Activated, 1);
	TestEqual(TEXT("Fallback executes while timed GE remains"), Second->Activated, BeforeTimedFallback + 1);
	Boss->Boss->BeginReset();
	TestTrue(TEXT("Reset does not refund a committed cooldown"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	TestTrue(TEXT("Cooldown reset completes"), Boss->Boss->CompleteReset());
	TestTrue(TEXT("Cooldown encounter restarts"), Boss->Boss->StartEncounter());
	Threat->AddThreat(Target, 10.f, Boss->Boss->GetBossState().EncounterId);
	TestTrue(TEXT("Restart still uses fallback before expiry"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Restart cannot bypass the committed cooldown"), Timed->Activated, 1);
	for (int32 Index = 0; Index < 20; ++Index)
	{
		TickWorld(.05f);
	}
	TestFalse(TEXT("GAS expiry removes the granted cooldown tag"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	TestEqual(TEXT("Expired cooldown effect is removed"), Boss->ASC->GetActiveEffects(TimedQuery).Num(), 0);
	TestTrue(TEXT("After real GE expiry selection returns to priority pattern"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Priority pattern activates again after expiry"), Timed->Activated, 2);
	TestTrue(TEXT("Next commit starts a fresh cooldown"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	Boss->ASC->RemoveActiveEffects(TimedQuery);
	Timed->bEndImmediately = false;
	TestTrue(TEXT("Cancellable timed pattern starts"), Boss->ASC->TryActivateAbility(TimedHandle, false));
	TimedAbility->FinishTest(true);
	TestTrue(TEXT("Cancellation does not refund a committed cooldown"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	Boss->ASC->RemoveActiveEffects(TimedQuery);
	Timed->bEndImmediately = true;
	const int32 BeforeInvalidConfiguration = Timed->Activated;

	TimedAbility->PatternCooldownTag = FGameplayTag();
	TestTrue(TEXT("Missing cooldown tag selects fallback"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Invalid cooldown configuration never activates"), Timed->Activated, BeforeInvalidConfiguration);
	TimedAbility->PatternCooldownTag = TimedTag;
	TimedAbility->PatternCooldownSeconds = -1.f;
	TestTrue(TEXT("Negative duration selects fallback"), Run() == EBTNodeResult::Succeeded);
	TimedAbility->PatternCooldownSeconds = 601.f;
	TestTrue(TEXT("Excessive duration selects fallback"), Run() == EBTNodeResult::Succeeded);
	TimedAbility->PatternCooldownSeconds = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Nonfinite duration selects fallback"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Rejected duration configurations never activate"), Timed->Activated, BeforeInvalidConfiguration);
	TimedAbility->PatternCooldownSeconds = 0.f;
	TestTrue(TEXT("Disabled optional cooldown retains ordinary activation"), Run() == EBTNodeResult::Succeeded);
	TestEqual(TEXT("Disabled cooldown creates no effect"), Boss->ASC->GetActiveEffects(TimedQuery).Num(), 0);
	TestFalse(TEXT("Disabled cooldown grants no tag"), Boss->ASC->HasMatchingGameplayTag(TimedTag));
	return true;
}
#endif
