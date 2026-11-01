#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossStartupTestFixture.h"

#include "AI/RPGBossAIComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Misc/AutomationTest.h"

class FRPGBossBattleSessionCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGBossBattleSessionCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FRPGBossBattleSessionCommand() override { Cleanup(); }

	virtual bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!Test->TestNotNull(TEXT("Boss battle world"), World)) { return true; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			Battle = World->SpawnActor<ARPGBossBattleGameState>();
			Boss = World->SpawnActor<ARPGBossStartupTestCharacter>();
			if (!Test->TestNotNull(TEXT("Battle state"), Battle) ||
				!Test->TestNotNull(TEXT("First boss"), Boss)) { return Finish(); }

			Test->TestEqual(TEXT("Fresh battle waits for a boss"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
			Battle->SetBoss(Boss);
			Test->TestTrue(TEXT("Server publishes its boss"), Battle->GetBattleSnapshot().Boss == Boss);
			Battle->StartBattle(60.f);
			Test->TestEqual(TEXT("Uninitialized boss cannot start the timer"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
			Boss->SpawnDefaultController();
			Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test->AddError(TEXT("Boss battle startup timed out"));
			return Finish();
		}
		if (!Boss->IsNPCStartUpDataReady()) { return false; }

		if (Stage == 0)
		{
			Test->TestTrue(TEXT("Ready boss starts its encounter"), Boss->BossAIComponent->StartEncounter());
			Battle->StartBattle(-1.f);
			Test->TestEqual(TEXT("Invalid duration is rejected"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
			Battle->StartBattle(60.f);
			Test->TestEqual(TEXT("Combat publishes server deadline"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::InProgress);
			Test->TestTrue(TEXT("Deadline is readable after start"), Battle->GetRemainingBattleTime() > 0.f);
			const int32 ActiveRevision = Battle->GetBattleSnapshot().Revision;
			Battle->StartBattle(60.f);
			Test->TestEqual(TEXT("Duplicate start cannot change the snapshot"),
				Battle->GetBattleSnapshot().Revision, ActiveRevision);
			Battle->FinishBattle(EBossBattleState::Victory);
			Battle->FinishBattle(EBossBattleState::Defeated);
			Test->TestEqual(TEXT("Live boss cannot be declared dead or wiped"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::InProgress);
			Test->TestTrue(TEXT("Server marks the wipe first"), Boss->BossAIComponent->MarkWiped());
			Battle->FinishBattle(EBossBattleState::Defeated);
			Test->TestEqual(TEXT("Wiped encounter publishes defeat"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Defeated);
			Battle->PrepareNextBattle();
			Test->TestEqual(TEXT("Living old boss blocks a new attempt"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Defeated);
			Boss->Destroy();
			Battle->PrepareNextBattle();
			Test->TestEqual(TEXT("Destroyed boss permits a new attempt"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
			Test->TestNull(TEXT("Old boss reference is cleared"), Battle->GetBattleSnapshot().Boss.Get());
			Boss = World->SpawnActor<ARPGBossStartupTestCharacter>();
			if (!Test->TestNotNull(TEXT("Second boss"), Boss)) { return Finish(); }
			Battle->SetBoss(Boss);
			Boss->SpawnDefaultController();
			Stage = 1;
			Deadline = FPlatformTime::Seconds() + 15.0;
			return false;
		}

		Test->TestTrue(TEXT("Second boss starts a fresh encounter"), Boss->BossAIComponent->StartEncounter());
		Battle->StartBattle(60.f);
		Boss->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(
			URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		Test->TestEqual(TEXT("Lethal health change marks boss dead"),
			Boss->BossAIComponent->GetBossState().EncounterState, ERPGBossEncounterState::Dead);
		Battle->FinishBattle(EBossBattleState::Victory);
		Test->TestEqual(TEXT("Dead boss publishes victory"),
			Battle->GetBattleSnapshot().BattleState, EBossBattleState::Victory);
		Boss->Destroy();
		Battle->PrepareNextBattle();
		Test->TestEqual(TEXT("Victory can also reset to waiting"),
			Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
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
	ARPGBossBattleGameState* Battle = nullptr;
	ARPGBossStartupTestCharacter* Boss = nullptr;
	int32 Stage = 0;
	double Deadline = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossBattleSessionTest,
	"ProjectRPG.Boss.BattleSession", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossBattleSessionTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGBossBattleSessionCommand(this));
	return true;
}

#endif
