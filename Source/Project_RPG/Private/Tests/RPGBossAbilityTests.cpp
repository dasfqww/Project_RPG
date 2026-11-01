#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossAbilityTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossAbilityTest, "ProjectRPG.Boss.Ability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossAbilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Ability test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Pawn);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	ARPGBossTestPawn* Target = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* TargetController = World->SpawnActor<AAIController>();
	TargetController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->Possess(Target);
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Pawn);
	Threat->RegisterComponent();
	URPGBossAbilityTestProbe* Probe = NewObject<URPGBossAbilityTestProbe>(Pawn);
	const FGameplayAbilitySpecHandle Handle = Pawn->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTestAbility::StaticClass(), 1, INDEX_NONE, Probe));
	URPGBossTestAbility* Ability = CastChecked<URPGBossTestAbility>(Pawn->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	// Simple automation runs inside one engine frame: the world tick manager deduplicates component ticks.
	// Advance world time, but drive the BT explicitly (the engine supports null tick function for unit tests).
	auto Tick = [World, Controller, Threat]()
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			World->Tick(LEVELTICK_All, .02f);
			Threat->RefreshTargets();
			if (UBehaviorTreeComponent* ActiveBT = Cast<UBehaviorTreeComponent>(Controller->BrainComponent))
			{
				if (ActiveBT->IsRunning()) { ActiveBT->TickComponent(.02f, LEVELTICK_All, nullptr); }
			}
		}
	};
	auto Start = [&](float Timeout = 120.f)
	{
		UBehaviorTreeComponent* BT = StartBossAbilityTestTree(Controller, Probe, Timeout);
		if (BT) { BT->SetComponentTickEnabled(false); }
		TestNotNull(TEXT("One-shot behavior tree"), BT);
		Tick();
		return BT;
	};
	auto CheckResult = [&](EBTNodeResult::Type Result)
	{
		TestTrue(TEXT("Task result"), Probe->LastResult == Result);
		TestFalse(TEXT("One-shot tree actually finished"), Controller->BrainComponent->IsRunning());
	};

	Start(); CheckResult(EBTNodeResult::Failed);
	TestEqual(TEXT("Idle cannot activate"), Probe->Activated, 0);
	Pawn->Boss->StartEncounter();
	Start(); CheckResult(EBTNodeResult::Failed);
	TestEqual(TEXT("Missing target cannot activate"), Probe->Activated, 0);
	Threat->AddThreat(Target, 10.f, Pawn->Boss->GetBossState().EncounterId);
	Probe->bBlockActivation = true;
	Start(); CheckResult(EBTNodeResult::Failed);
	TestEqual(TEXT("GAS activation denial"), Probe->Activated, 0);
	Probe->bBlockActivation = false;
	Probe->bEndImmediately = true;
	Start(); CheckResult(EBTNodeResult::Succeeded);
	TestEqual(TEXT("Synchronous completion activates once"), Probe->Activated, 1);
	TestTrue(TEXT("Ability captures server target"), Probe->SeenTarget.Get() == Target);
	Probe->bEndImmediately = false;
	Probe->bCancelImmediately = true;
	Start(); CheckResult(EBTNodeResult::Failed);
	Probe->bCancelImmediately = false;
	UBehaviorTreeComponent* BT = Start();
	TestTrue(TEXT("Latent ability stays active"), Ability->IsActive());
	TestTrue(TEXT("Context is valid while running"), Ability->IsBossExecutionValid());
	const int32 FinishedBefore = Probe->FinishedTasks;
	Ability->FinishTest(false); Tick(); CheckResult(EBTNodeResult::Succeeded);
	TestEqual(TEXT("Latent completion finishes once"), Probe->FinishedTasks, FinishedBefore + 1);
	TestNull(TEXT("Completed ability exposes no target"), Ability->GetBossExecutionTarget());
	BT = Start();
	BT->StopTree(EBTStopMode::Safe); Tick();
	CheckResult(EBTNodeResult::Aborted);
	TestFalse(TEXT("BT abort cancels its ability"), Ability->IsActive());
	Start(.01f);
	CheckResult(EBTNodeResult::Failed);
	TestFalse(TEXT("Timeout cancels ability"), Ability->IsActive());
	BT = Start();
	Pawn->SetTestHealth(69.f);
	TestFalse(TEXT("Phase change cancels immediately"), Ability->IsActive());
	Tick(); CheckResult(EBTNodeResult::Failed);
	BT = Start();
	Pawn->Boss->BeginReset();
	TestFalse(TEXT("Reset cancels immediately"), Ability->IsActive());
	Tick(); CheckResult(EBTNodeResult::Failed);
	Pawn->SetTestHealth(100.f);
	Pawn->Boss->CompleteReset(); Pawn->Boss->StartEncounter();
	Threat->AddThreat(Target, 10.f, Pawn->Boss->GetBossState().EncounterId);
	BT = Start();
	TestEqual(TEXT("New execution captures new encounter"), Ability->GetBossExecutionEncounterId(), 2);
	TargetController->UnPossess();
	TestFalse(TEXT("Disconnect invalidates hit context immediately"), Ability->IsBossExecutionValid());
	Tick(); CheckResult(EBTNodeResult::Failed);
	TestFalse(TEXT("Disconnect cancels on BT tick"), Ability->IsActive());
	TargetController->Possess(Target);
	Threat->AddThreat(Target, 10.f, 2);
	BT = Start();
	AAIController* ReplacementTargetController = World->SpawnActor<AAIController>();
	ReplacementTargetController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->UnPossess();
	ReplacementTargetController->Possess(Target);
	TestTrue(TEXT("Replacement controller may establish fresh threat"), Threat->AddThreat(Target, 10.f, 2));
	TestTrue(TEXT("Same Pawn is again the selected target"), Threat->GetCurrentTarget() == Target);
	TestFalse(TEXT("Fresh threat cannot revive an attack captured for the old target controller"), Ability->IsBossExecutionValid());
	Tick(); CheckResult(EBTNodeResult::Failed);
	TestFalse(TEXT("BT cancels the old target-controller execution"), Ability->IsActive());
	BT = Start();
	TestTrue(TEXT("New execution captures replacement target controller"), Ability->IsActive() && Ability->IsBossExecutionValid());
	Ability->FinishTest(false); Tick(); CheckResult(EBTNodeResult::Succeeded);
	ReplacementTargetController->UnPossess();
	TargetController->Possess(Target);
	Threat->AddThreat(Target, 10.f, 2);
	// Never adopt or cancel an ability that was already running before this task.
	TestTrue(TEXT("External server activation"), Pawn->ASC->TryActivateAbility(Handle, false));
	BT = Start(); CheckResult(EBTNodeResult::Failed);
	TestTrue(TEXT("Rejected task leaves foreign execution alone"), Ability->IsActive());
	Ability->FinishTest(false);
	// End callback can restart the same spec; old task cleanup must not cancel the new run.
	BT = Start();
	const uint64 PreviousSerial = Ability->GetBossExecutionSerial();
	Ability->FinishTest(false);
	TestTrue(TEXT("Immediate subsequent activation"), Pawn->ASC->TryActivateAbility(Handle, false));
	Tick();
	TestTrue(TEXT("Cleanup preserves subsequent activation"), Ability->IsActive() && Ability->GetBossExecutionSerial() == PreviousSerial + 1);
	Ability->FinishTest(false);
	Pawn->SetTestRole(ROLE_SimulatedProxy);
	BT = Start(); CheckResult(EBTNodeResult::Failed);
	TestFalse(TEXT("Client role cannot activate"), Ability->IsActive());
	Pawn->SetTestRole(ROLE_Authority);
	BT = Start();
	Pawn->SetTestHealth(0.f);
	TestFalse(TEXT("Boss death cancels immediately"), Ability->IsActive());
	Tick(); CheckResult(EBTNodeResult::Failed);
	BT->StopTree(EBTStopMode::Forced);
	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
