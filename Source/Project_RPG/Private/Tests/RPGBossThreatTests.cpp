#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossThreatTest,
	"ProjectRPG.Boss.Threat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossThreatTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Threat test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	if (!TestNotNull(TEXT("Threat world AI system"), World->CreateAISystem()))
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		return false;
	}
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	auto Spawn = [World](uint8 Team)
	{
		ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
		AAIController* Controller = World->SpawnActor<ARPGAIController>();
		Controller->SetGenericTeamId(FGenericTeamId(Team));
		Controller->Possess(Pawn);
		return Pawn;
	};
	ARPGBossTestPawn* Owner = Spawn(1);
	ARPGBossTestPawn* First = Spawn(0);
	ARPGBossTestPawn* Second = Spawn(0);
	ARPGBossTestPawn* Friendly = Spawn(1);
	AAIController* Controller = CastChecked<AAIController>(Owner->GetController());
	UBlackboardData* Data = NewObject<UBlackboardData>(Controller);
	FBlackboardEntry Key;
	Key.EntryName = TEXT("TargetActor");
	UBlackboardKeyType_Object* ObjectKey = NewObject<UBlackboardKeyType_Object>(Data);
	ObjectKey->BaseClass = APawn::StaticClass();
	Key.KeyType = ObjectKey;
	Data->Keys.Add(Key);
	UBlackboardComponent* Blackboard = nullptr;
	TestTrue(TEXT("Create target Blackboard"), Controller->UseBlackboard(Data, Blackboard));
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Owner);
	Threat->RegisterComponent();
	TestFalse(TEXT("Idle encounters reject threat"), Threat->AddThreat(First, 10.f, 0));
	UAIPerceptionComponent* Perception = Controller->GetPerceptionComponent();
	const UAISense_Sight* Sight = GetDefault<UAISense_Sight>();
	Perception->RegisterStimulus(First, FAIStimulus(*Sight, 1.f, First->GetActorLocation(), Owner->GetActorLocation()));
	Perception->ProcessStimuli();
	TestNull(TEXT("Sight does not start an idle encounter"), Threat->GetCurrentTarget());
	TestTrue(TEXT("Start threat encounter"), Owner->Boss->StartEncounter());
	const int32 EncounterId = Owner->Boss->GetBossState().EncounterId;
	Threat->RefreshTargets();
	TestTrue(TEXT("Refresh acquires targets seen before combat began"), Threat->GetCurrentTarget() == First);
	TestEqual(TEXT("Sight seeds minimum threat"), Threat->GetThreatForTarget(First), 1.0);
	Perception->RegisterStimulus(First, FAIStimulus(*Sight, 0.f, First->GetActorLocation(), Owner->GetActorLocation(), FAIStimulus::SensingFailed));
	Perception->ProcessStimuli();
	TestNull(TEXT("Controller sight-loss callback clears target"), Threat->GetCurrentTarget());
	TestFalse(TEXT("Friendly target rejected"), Threat->AddThreat(Friendly, 100.f, EncounterId));
	TestFalse(TEXT("Self target rejected"), Threat->AddThreat(Owner, 100.f, EncounterId));
	TestFalse(TEXT("Zero rejected"), Threat->AddThreat(First, 0.f, EncounterId));
	TestFalse(TEXT("Negative rejected"), Threat->AddThreat(First, -1.f, EncounterId));
	TestFalse(TEXT("NaN rejected"), Threat->AddThreat(First, std::numeric_limits<float>::quiet_NaN(), EncounterId));
	TestFalse(TEXT("Infinity rejected"), Threat->AddThreat(First, std::numeric_limits<float>::infinity(), EncounterId));
	TestTrue(TEXT("First threat admitted"), Threat->AddThreat(First, 10.f, EncounterId));
	TestTrue(TEXT("Second threat admitted"), Threat->AddThreat(Second, 10.f, EncounterId));
	TestTrue(TEXT("Ties retain current target"), Threat->GetCurrentTarget() == First);
	Threat->AddThreat(Second, 1.f, EncounterId);
	TestTrue(TEXT("Small increases do not cause target oscillation"), Threat->GetCurrentTarget() == First);
	Threat->AddThreat(Second, 1.f, EncounterId);
	TestTrue(TEXT("Higher threat switches target"), Threat->GetCurrentTarget() == Second);
	if (Blackboard) { TestTrue(TEXT("Blackboard mirrors selection"), Blackboard->GetValueAsObject(TEXT("TargetActor")) == Second); }
	Owner->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot add threat"), Threat->AddThreat(First, 1000.f, EncounterId));
	TestFalse(TEXT("Client cannot clear server ledger"), Threat->ClearThreat());
	TestNull(TEXT("Client cannot select a target"), Threat->GetCurrentTarget());
	Owner->SetTestRole(ROLE_Authority);
	TestEqual(TEXT("Client calls preserved score"), Threat->GetThreatForTarget(Second), 12.0);
	Threat->NotifySightTarget(Second, true);
	Threat->NotifySightTarget(Second, true);
	TestEqual(TEXT("Repeated sight updates do not farm threat"), Threat->GetThreatForTarget(Second), 12.0);
	Threat->NotifySightTarget(Second, false);
	TestTrue(TEXT("Sight loss selects a fallback"), Threat->GetCurrentTarget() == First);
	First->SetTestHealth(0.f);
	TestNull(TEXT("Getter rejects death before the periodic refresh"), Threat->GetCurrentTarget());
	Threat->RefreshTargets();
	if (Blackboard) { TestNull(TEXT("Refresh clears dead target in Blackboard"), Blackboard->GetValueAsObject(TEXT("TargetActor"))); }
	Threat->AddThreat(Second, 20.f, EncounterId);
	AController* SecondController = Second->GetController();
	SecondController->UnPossess();
	TestNull(TEXT("Disconnected/unpossessed target is immediately ineligible"), Threat->GetCurrentTarget());
	Threat->RefreshTargets();
	SecondController->Possess(Second);
	TestEqual(TEXT("Unpossessed target loses prior threat"), Threat->GetThreatForTarget(Second), 0.0);
	Threat->AddThreat(Second, 20.f, EncounterId);
	AAIController* Replacement = World->SpawnActor<AAIController>();
	Replacement->SetGenericTeamId(FGenericTeamId(0));
	SecondController->UnPossess();
	Replacement->Possess(Second);
	TestNull(TEXT("Controller change cannot inherit old threat"), Threat->GetCurrentTarget());
	Threat->AddThreat(Second, 2.f, EncounterId);
	TestEqual(TEXT("New controller starts from fresh score"), Threat->GetThreatForTarget(Second), 2.0);
	Second->SetActorLocation(FVector(10000.f, 0.f, 0.f));
	TestNull(TEXT("Out of range target is ineligible"), Threat->GetCurrentTarget());
	Threat->RefreshTargets();
	Second->SetActorLocation(FVector::ZeroVector);
	TestEqual(TEXT("Range exit discarded score"), Threat->GetThreatForTarget(Second), 0.0);
	First->SetTestHealth(100.f);
	APlayerState* TargetState = World->SpawnActor<APlayerState>();
	First->SetPlayerState(TargetState);
	TargetState->SetIsInactive(true);
	TestFalse(TEXT("Inactive player rejected"), Threat->AddThreat(First, 1.f, EncounterId));
	TargetState->SetIsInactive(false);
	TargetState->SetIsSpectator(true);
	TestFalse(TEXT("Spectator rejected"), Threat->AddThreat(First, 1.f, EncounterId));
	TargetState->SetIsSpectator(false);
	Threat->AddThreat(First, 1.f, EncounterId);
	if (Blackboard)
	{
		bool bClearObserverCalled = false;
		const FBlackboard::FKey TargetKey = Blackboard->GetKeyID(TEXT("TargetActor"));
		const FDelegateHandle Observer = Blackboard->RegisterObserver(TargetKey, Owner,
			FOnBlackboardChangeNotification::CreateLambda([&](const UBlackboardComponent&, FBlackboard::FKey)
			{
				bClearObserverCalled = true;
				TestFalse(TEXT("Blackboard observer cannot repopulate while clearing"), Threat->AddThreat(Second, 100.f, EncounterId));
				return EBlackboardNotificationResult::ContinueObserving;
			}));
		Threat->ClearThreat();
		TestTrue(TEXT("Clear publishes target removal"), bClearObserverCalled);
		TestNull(TEXT("Clear remains empty after observers"), Threat->GetCurrentTarget());
		Blackboard->UnregisterObserver(TargetKey, Observer);
	}
	else { Threat->ClearThreat(); }
	Threat->MaxTrackedTargets = 1;
	TestTrue(TEXT("Capacity admits first target"), Threat->AddThreat(First, 1.f, EncounterId));
	TestFalse(TEXT("Capacity rejects excess target"), Threat->AddThreat(Second, 1.f, EncounterId));
	Owner->Boss->MarkWiped();
	TestNull(TEXT("Wipe clears target immediately"), Threat->GetCurrentTarget());
	if (Blackboard) { TestNull(TEXT("Wipe clears Blackboard immediately"), Blackboard->GetValueAsObject(TEXT("TargetActor"))); }
	TestFalse(TEXT("Wiped encounter rejects threat"), Threat->AddThreat(Second, 1.f, EncounterId));
	Owner->Boss->BeginReset();
	Owner->Boss->CompleteReset();
	Owner->Boss->StartEncounter();
	TestFalse(TEXT("Delayed prior-encounter hit is rejected"), Threat->AddThreat(First, 1000.f, EncounterId));
	TestEqual(TEXT("New encounter starts empty"), Threat->GetThreatForTarget(First), 0.0);
	TestTrue(TEXT("New encounter accepts new hit"), Threat->AddThreat(Second, 1.f, EncounterId + 1));
	Second->Destroy();
	TestNull(TEXT("Destroyed target is rejected"), Threat->GetCurrentTarget());
	Threat->RefreshTargets();
	Threat->AddThreat(First, 1.f, EncounterId + 1);
	Owner->SetTestHealth(0.f);
	TestNull(TEXT("Boss death clears target"), Threat->GetCurrentTarget());
	ARPGAIController* RPGController = World->SpawnActor<ARPGAIController>();
	AActor* NonPawn = World->SpawnActor<AActor>();
	TestTrue(TEXT("Team query safely handles non-Pawn actors"), RPGController->GetTeamAttitudeTowards(*NonPawn) == ETeamAttitude::Neutral);
	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
