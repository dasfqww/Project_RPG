#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossBattleRetryTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"

class FRPGBossBattleRetryCommand : public IAutomationLatentCommand
{
public:
	explicit FRPGBossBattleRetryCommand(FAutomationTestBase* InTest) : Test(InTest) {}
	virtual ~FRPGBossBattleRetryCommand() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!Test->TestNotNull(TEXT("Retry world"), World)) { return true; }
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			World->SpawnActor<ATargetPoint>();
			Mode = World->SpawnActorDeferred<ARPGBossRetryTestGameMode>(
				ARPGBossRetryTestGameMode::StaticClass(), FTransform::Identity);
			if (!Test->TestNotNull(TEXT("Deferred retry GameMode"), Mode)) { return Finish(); }
			Mode->ConfigureStartup();
			Mode->FinishSpawning(FTransform::Identity);
			// AGameModeBase::PreInitializeComponents creates the real GameState
			// during FinishSpawning; do not keep a separate pre-spawn snapshot.
			Battle = Mode->GetGameState<ARPGBossBattleGameState>();
			if (!Test->TestNotNull(TEXT("Production startup GameState"), Battle)) { return Finish(); }
			Test->TestEqual(TEXT("Production BeginPlay discards legacy premature InProgress"),
				Mode->GetCurrentBossBattleState(), EBossBattleState::Waiting);
			Test->TestEqual(TEXT("No ready players means no replicated battle start"),
				Battle->GetBattleSnapshot().BattleState, EBossBattleState::Waiting);
			First = AddPlayer(FVector(100.f, 0.f, 0.f));
			Outsider = AddPlayer(FVector(20000.f, 0.f, 0.f));
			Deadline = FPlatformTime::Seconds() + 20.0;
		}
		if (FPlatformTime::Seconds() > Deadline)
		{
			Test->AddError(TEXT("Retry boss startup timed out"));
			return Finish();
		}
		ARPGBossStartupTestCharacter* Boss = Cast<ARPGBossStartupTestCharacter>(Battle->GetBattleSnapshot().Boss);
		if (!Test->TestNotNull(TEXT("Current retry boss"), Boss)) { return Finish(); }
		if (!Boss->GetController()) { Boss->SpawnDefaultController(); }
		if (!Boss->IsNPCStartUpDataReady()) { return false; }
		Mode->Tick(.1f);
		if (!Test->TestEqual(TEXT("Ready participants start production GameMode"),
			Mode->GetCurrentBossBattleState(), EBossBattleState::InProgress)) { return Finish(); }

		if (Stage == 0)
		{
			Mode->Victory(Boss);
			Test->TestEqual(TEXT("Living boss cannot submit a clear"), Mode->ClearReportCount, 0);
			Test->TestEqual(TEXT("Out-of-arena player is not registered"), Mode->GetParticipantCount(), 1);
			Second = AddPlayer(FVector(200.f, 0.f, 0.f));
			Mode->Tick(.1f);
			Test->TestEqual(TEXT("Late participant joins running battle"), Mode->GetParticipantCount(), 2);
			Test->TestFalse(TEXT("Retry cannot run during combat"), Mode->TryRestartBattleForParticipant(First));
			Kill(First);
			Mode->Tick(.1f);
			Test->TestEqual(TEXT("One surviving participant prevents wipe"), Mode->GetCurrentBossBattleState(), EBossBattleState::InProgress);
			Kill(Second);
			Mode->Tick(.1f);
			Test->TestEqual(TEXT("All registered players dead causes defeat"), Mode->GetCurrentBossBattleState(), EBossBattleState::Defeated);
			Test->TestEqual(TEXT("Defeat cancels the boss encounter"), Boss->BossAIComponent->GetBossState().EncounterState, ERPGBossEncounterState::Wiped);
			Test->TestFalse(TEXT("Living outsider cannot authorize retry"), Mode->TryRestartBattleForParticipant(Outsider));
			const int32 DefeatRevision = Battle->GetBattleSnapshot().Revision;
			for (const int32 Revision : { DefeatRevision - 1, DefeatRevision + 1, -2 })
				Test->TestFalse(TEXT("Stale, future and invalid revision cannot mutate defeat"), Mode->TryRestartBattleForParticipant(First, Revision));
			Test->TestTrue(TEXT("Rejected revision preserves current boss"), Battle->GetBattleSnapshot().Boss == Boss);
			Test->TestEqual(TEXT("Rejected revision preserves result"), Battle->GetBattleSnapshot().Revision, DefeatRevision);
			First->GetPawn()->Destroy();
			TWeakObjectPtr<APawn> Orphan = Second->GetPawn();
			Second->UnPossess();
			Mode->bFailPlayerSpawn = true;
			Test->TestFalse(TEXT("Failed player spawn denies retry completion"), Mode->TryRestartBattleForParticipant(First));
			Test->TestEqual(TEXT("Failed recovery preserves result"), Battle->GetBattleSnapshot().BattleState, EBossBattleState::Defeated);
			Test->TestTrue(TEXT("Failed recovery does not replace boss"), Battle->GetBattleSnapshot().Boss == Boss);
			Mode->bFailPlayerSpawn = false;
			Test->TestTrue(TEXT("Registered player can retry current revision after spawn is repaired"), Mode->TryRestartBattleForParticipant(First, DefeatRevision));
			Test->TestFalse(TEXT("Only unpossessed old pawn is retired"), Orphan.IsValid());
			VerifyRecovered(First, FVector(100.f, 0.f, 0.f));
			VerifyRecovered(Second, FVector(200.f, 0.f, 0.f));
			Test->TestTrue(TEXT("Retry replaces the boss"), Battle->GetBattleSnapshot().Boss != Boss);
			Test->TestFalse(TEXT("Duplicate retry cannot restart waiting battle"), Mode->TryRestartBattleForParticipant(First));
			Stage = 1;
			Deadline = FPlatformTime::Seconds() + 20.0;
			return false;
		}
		if (Stage == 2)
		{
			Mode->bOnlineDungeon = true;
			Boss->GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
			Test->TestEqual(TEXT("Native boss death commits victory"), Mode->GetCurrentBossBattleState(), EBossBattleState::Victory);
			Test->TestEqual(TEXT("Native victory reports party clear once"), Mode->ClearReportCount, 1);
			Mode->Victory(Boss);
			Mode->SetBattleState(EBossBattleState::Victory);
			Test->TestEqual(TEXT("Repeated victory does not report another clear"), Mode->ClearReportCount, 1);
			Test->TestFalse(TEXT("Online rewarded victory cannot retry the completed backend session"), Mode->TryRestartBattleForParticipant(Second));
			Test->TestTrue(TEXT("Online retry rejection retains the completed boss"), Battle->GetBattleSnapshot().Boss == Boss);
			Mode->bOnlineDungeon = false;
			Test->TestTrue(TEXT("Standalone victory retry remains supported"), Mode->TryRestartBattleForParticipant(Second));
			return Finish();
		}
		Test->TestEqual(TEXT("Respawned players join the new encounter"), Mode->GetParticipantCount(), 2);
		// Logout while the pawn is still alive must not count that pawn as a survivor.
		Mode->Logout(First);
		First->Destroy();
		Test->TestEqual(TEXT("Logout removes participant authorization"), Mode->GetParticipantCount(), 1);
		Kill(Second);
		Mode->Tick(.1f);
		Test->TestEqual(TEXT("Last participant death wipes despite outsider"), Mode->GetCurrentBossBattleState(), EBossBattleState::Defeated);
		ARPGBossRetryTestPlayer* ExistingPawn = CastChecked<ARPGBossRetryTestPlayer>(Second->GetPawn());
		const uint32 RecoveryRevisionBefore = ExistingPawn->GetCombatPresentationRevision();
		const ECollisionEnabled::Type PreviousCollision = ExistingPawn->GetCapsuleComponent()->GetCollisionEnabled();
		ExistingPawn->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ExistingPawn->GetMesh()->bPauseAnims = true;
		ExistingPawn->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Mode->DisablePlayerMovement();
		Test->TestTrue(TEXT("An existing dead pawn can also retry"), Mode->TryRestartBattleForParticipant(Second));
		Test->TestTrue(TEXT("Existing pawn is not unnecessarily replaced"), Second->GetPawn() == ExistingPawn);
		Test->TestEqual(TEXT("Same-pawn retry invalidates previous combat presentation"),
			ExistingPawn->GetCombatPresentationRevision(), RecoveryRevisionBefore + 1);
		Test->TestEqual(TEXT("Existing pawn is not possessed/granted again"), ExistingPawn->PossessionCount, 1);
		Test->TestEqual(TEXT("Saved participant movement mode is preserved"), ExistingPawn->GetCharacterMovement()->MovementMode, TEnumAsByte<EMovementMode>(MOVE_Flying));
		Test->TestEqual(TEXT("Existing pawn health is restored"), ExistingPawn->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
		Test->TestEqual(TEXT("Death-disabled capsule is restored"), ExistingPawn->GetCapsuleComponent()->GetCollisionEnabled(), PreviousCollision);
		Test->TestFalse(TEXT("Death-paused animation is restored"), ExistingPawn->GetMesh()->bPauseAnims);
		Test->TestEqual(TEXT("Wipes and retries have not submitted a clear"), Mode->ClearReportCount, 0);
		Stage = 2;
		Deadline = FPlatformTime::Seconds() + 20.0;
		return false;
	}
private:
	APlayerController* AddPlayer(const FVector& Location)
	{
		APlayerController* Controller = World->SpawnActor<APlayerController>();
		ARPGBossRetryTestPlayer* Pawn = World->SpawnActor<ARPGBossRetryTestPlayer>(Location, FRotator::ZeroRotator);
		Controller->Possess(Pawn);
		return Controller;
	}
	void Kill(APlayerController* Controller)
	{
		URPGAbilitySystemComponent* ASC = CastChecked<ARPGPlayer>(Controller->GetPawn())->GetRPGAbilitySystemComponent();
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
	}
	void VerifyRecovered(APlayerController* Controller, const FVector& EntryLocation)
	{
		ARPGBossRetryTestPlayer* Pawn = Cast<ARPGBossRetryTestPlayer>(Controller->GetPawn());
		if (!Test->TestNotNull(TEXT("New player pawn is possessed"), Pawn)) { return; }
		Test->TestTrue(TEXT("Recovery uses registered arena entry"), Pawn->GetActorLocation().Equals(EntryLocation));
		Test->TestEqual(TEXT("Fresh pawn startup occurs once"), Pawn->PossessionCount, 1);
		Test->TestEqual(TEXT("Recovered player has full GAS health"), Pawn->GetRPGAttributeSet()->GetCurrentHealth(), 100.f);
		Test->TestFalse(TEXT("Recovered pawn is not dead"), Pawn->GetAbilitySystemComponent()->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death));
	}
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
	ARPGBossRetryTestGameMode* Mode = nullptr;
	ARPGBossBattleGameState* Battle = nullptr;
	APlayerController* First = nullptr;
	APlayerController* Second = nullptr;
	APlayerController* Outsider = nullptr;
	int32 Stage = 0;
	double Deadline = 0.0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossBattleRetryTest, "ProjectRPG.Boss.BattleRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossBattleRetryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRPGBossBattleRetryCommand(this));
	return true;
}
#endif
