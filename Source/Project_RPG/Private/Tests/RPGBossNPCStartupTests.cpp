#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossStartupTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

class FRPGBossNPCStartupCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGBossNPCStartupCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FRPGBossNPCStartupCommand() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!Test->TestNotNull(TEXT("NPC startup world"), World)) { return true; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->CreateAISystem();
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			Manual = World->SpawnActor<ARPGBossStartupTestCharacter>();
			Auto = World->SpawnActor<ARPGBossStartupTestCharacter>();
			Canceled = World->SpawnActor<ARPGBossStartupTestCharacter>();
			ARPGBossStartupTestCharacter* Destroyed = World->SpawnActor<ARPGBossStartupTestCharacter>();
			if (!Test->TestNotNull(TEXT("Manual boss"), Manual) || !Test->TestNotNull(TEXT("Automatic boss"), Auto) ||
				!Test->TestNotNull(TEXT("Canceled boss"), Canceled) || !Test->TestNotNull(TEXT("Destroyed boss"), Destroyed))
			{
				return Finish();
			}
			Test->TestFalse(TEXT("ASC actor info alone cannot start an NPC encounter"), Manual->BossAIComponent->StartEncounter());
			Manual->TestData->bRepossessDuringGrant = true;
			Auto->SetAutoStartForTest(true);
			Manual->SpawnDefaultController();
			Auto->SpawnDefaultController();
			Canceled->SpawnDefaultController();
			Destroyed->SpawnDefaultController();
			if (!Test->TestNotNull(TEXT("Canceled controller"), Canceled->GetController())) { return Finish(); }
			CanceledController = Canceled->GetController();
			CanceledController->UnPossess();
			DestroyedData.Reset(Destroyed->TestData);
			Destroyed->Destroy();
			Test->TestFalse(TEXT("Loaded assets still complete asynchronously"), Manual->IsNPCStartUpDataReady());
			Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test->AddError(TEXT("NPC startup callback timed out"));
			return Finish();
		}
		if (Stage == 0)
		{
			if (!Manual->IsNPCStartUpDataReady() || !Auto->IsNPCStartUpDataReady()) { return false; }
			Test->TestEqual(TEXT("Reentrant possession cannot double grant"), Manual->TestData->GrantCount, 1);
			Test->TestEqual(TEXT("Ready notification is emitted once"), Manual->ReadyCount, 1);
			Test->TestTrue(TEXT("Grant-time encounter start is rejected"), Manual->TestData->bEarlyStartDenied);
			Test->TestEqual(TEXT("Manual mode remains idle after ready"), Manual->BossAIComponent->GetBossState().EncounterId, 0);
			Test->TestEqual(TEXT("Opt-in automatic mode starts once"), Auto->BossAIComponent->GetBossState().EncounterId, 1);
			Test->TestEqual(TEXT("Unpossessed pending grant is canceled"), Canceled->TestData->GrantCount, 0);
			Test->TestFalse(TEXT("Unpossessed NPC is not ready"), Canceled->IsNPCStartUpDataReady());
			Test->TestEqual(TEXT("Destroyed pending grant is canceled"), DestroyedData->GrantCount, 0);
			Manual->SetRoleForTest(ROLE_SimulatedProxy);
			Test->TestFalse(TEXT("Client cannot claim startup readiness"), Manual->IsNPCStartUpDataReady());
			Test->TestFalse(TEXT("Client cannot start initialized NPC"), Manual->BossAIComponent->StartEncounter());
			Manual->SetRoleForTest(ROLE_Authority);
			Test->TestTrue(TEXT("Server can explicitly start ready NPC"), Manual->BossAIComponent->StartEncounter());
			UBehaviorTree* RecoveryTree = NewObject<UBehaviorTree>(Manual);
			RecoveryTree->BlackboardAsset = NewObject<UBlackboardData>(RecoveryTree);
			RecoveryTree->RootNode = NewObject<UBTComposite_Sequence>(RecoveryTree);
			UBTTask_Wait* Wait = NewObject<UBTTask_Wait>(RecoveryTree);
			Wait->WaitTime = 30.f;
			Wait->RandomDeviation = 0.f;
			RecoveryTree->RootNode->Children.AddDefaulted_GetRef().ChildTask = Wait;
			AAIController* RecoveryAI = CastChecked<AAIController>(Manual->GetController());
			Test->TestTrue(TEXT("Recovery test starts an actual BT"), RecoveryAI->RunBehaviorTree(RecoveryTree));
			UBrainComponent* RecoveryBrain = RecoveryAI->GetBrainComponent();
			if (!Test->TestNotNull(TEXT("Recovery brain"), RecoveryBrain)) { return Finish(); }
			Manual->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Manual->GetCharacterMovement()->Velocity = FVector(100.f, 0.f, 0.f);
			RecoveryAI->SetFocus(Auto, EAIFocusPriority::Gameplay);
			Test->TestTrue(TEXT("Server wipes the running NPC"), Manual->BossAIComponent->MarkWiped());
			Test->TestTrue(TEXT("Wipe immediately stops movement"), Manual->GetCharacterMovement()->Velocity.IsNearlyZero() &&
				Manual->GetCharacterMovement()->MovementMode == MOVE_None);
			Test->TestFalse(TEXT("Wipe stops the running brain"), RecoveryBrain->IsRunning());
			Test->TestNull(TEXT("Wipe clears gameplay focus"), RecoveryAI->GetFocusActorForPriority(EAIFocusPriority::Gameplay));
			Test->TestTrue(TEXT("Manual boss enters reset"), Manual->BossAIComponent->BeginReset());
			Test->TestTrue(TEXT("Manual boss finishes reset"), Manual->BossAIComponent->CompleteReset());
			Test->TestFalse(TEXT("Idle after cleanup does not restart the brain"), RecoveryBrain->IsRunning());
			Test->TestTrue(TEXT("Idle remains stopped until explicit combat start"), Manual->GetCharacterMovement()->MovementMode == MOVE_None);
			Test->TestTrue(TEXT("Same NPC can explicitly begin another fight"), Manual->BossAIComponent->StartEncounter());
			Test->TestTrue(TEXT("Next fight restores the actual prior flying mode"), Manual->GetCharacterMovement()->MovementMode == MOVE_Flying);
			Test->TestTrue(TEXT("Next fight restarts the brain stopped by this NPC"), RecoveryBrain->IsRunning());
			RecoveryBrain->PauseLogic(TEXT("External test owner"));
			Manual->BossAIComponent->BeginReset(); Manual->BossAIComponent->CompleteReset(); Manual->BossAIComponent->StartEncounter();
			Test->TestTrue(TEXT("An external brain pause is not resumed by encounter restart"), RecoveryBrain->IsPaused());
			RecoveryBrain->ResumeLogic(TEXT("External test owner finished"));
			Manual->BossAIComponent->BeginReset(); Manual->BossAIComponent->CompleteReset();
			AController* ManualController = Manual->GetController();
			ManualController->UnPossess();
			Test->TestFalse(TEXT("Ready but unpossessed NPC cannot restart"), Manual->BossAIComponent->StartEncounter());
			ManualController->Possess(Manual);
			Test->TestTrue(TEXT("Ready NPC may restart after repossession"), Manual->BossAIComponent->StartEncounter());
			Test->TestEqual(TEXT("Repossessing ready NPC retains one grant"), Manual->TestData->GrantCount, 1);
			Test->TestTrue(TEXT("Automatic boss enters reset"), Auto->BossAIComponent->BeginReset());
			Test->TestTrue(TEXT("Automatic boss finishes reset"), Auto->BossAIComponent->CompleteReset());
			AController* AutoController = Auto->GetController();
			AutoController->UnPossess();
			AutoController->Possess(Auto);
			Test->TestTrue(TEXT("Reset does not auto-restart on possession"),
				Auto->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Idle);
			CanceledController->Possess(Canceled);
			CanceledController->UnPossess();
			CanceledController->Possess(Canceled);
			Stage = 1;
			return false;
		}
		if (!Canceled->IsNPCStartUpDataReady()) { return false; }
		Test->TestEqual(TEXT("Only the newest pending request grants data"), Canceled->TestData->GrantCount, 1);
		Test->TestEqual(TEXT("Replacement load has one ready notification"), Canceled->ReadyCount, 1);
		Test->TestEqual(TEXT("Dead owner's delayed callback stays canceled"), DestroyedData->GrantCount, 0);
		// Terminal cleanup uses the actual boss base, not a fixture that pre-stops movement.
		Manual->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Manual->GetCharacterMovement()->Velocity = FVector(100.f, 0.f, 0.f);
		Manual->GetAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		Test->TestTrue(TEXT("Zero HP drives terminal boss state"), Manual->BossAIComponent->GetBossState().EncounterState == ERPGBossEncounterState::Dead);
		Test->TestTrue(TEXT("Death immediately clears velocity and movement mode"),
			Manual->GetCharacterMovement()->Velocity.IsNearlyZero() && Manual->GetCharacterMovement()->MovementMode == MOVE_None);
		AController* DeadController = Manual->GetController();
		DeadController->UnPossess();
		Manual->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		DeadController->Possess(Manual);
		Test->TestTrue(TEXT("Repossessing a terminal boss cannot restore movement"), Manual->GetCharacterMovement()->MovementMode == MOVE_None);
		Test->TestEqual(TEXT("Death repossession cannot grant startup again"), Manual->TestData->GrantCount, 1);
		return Finish();
	}
private:
	bool Finish() { Cleanup(); return true; }
	void Cleanup()
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			World = nullptr;
		}
	}
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	ARPGBossStartupTestCharacter* Manual = nullptr;
	ARPGBossStartupTestCharacter* Auto = nullptr;
	ARPGBossStartupTestCharacter* Canceled = nullptr;
	AController* CanceledController = nullptr;
	TStrongObjectPtr<URPGBossStartupTestData> DestroyedData;
	int32 Stage = 0;
	double Deadline = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossNPCStartupTest,
	"ProjectRPG.Boss.NPCStartup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossNPCStartupTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGBossNPCStartupCommand(this));
	return true;
}

#endif
