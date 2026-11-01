#if WITH_DEV_AUTOMATION_TESTS
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

struct FRPGBossBattleRetryRequestTestAccess
{
	static void Arm(ARPGPlayerController& Controller, uint32 Id, UWorld* World, int32 Revision, double Deadline)
	{
		Controller.PendingBossBattleRetryId = Id;
		Controller.PendingBossBattleRetryWorld = World;
		Controller.PendingBossBattleRetryRevision = Revision;
		Controller.BossBattleRetryDeadline = Deadline;
		Controller.BossBattleRestartError.Reset();
	}
	static void Reply(ARPGPlayerController& Controller, uint32 Id, bool bAccepted)
	{
		Controller.ClientBossBattleRestartResult_Implementation(Id, bAccepted);
	}
	static void Update(ARPGPlayerController& Controller) { Controller.UpdateBossBattleRetryRequest(); }
	static void End(ARPGPlayerController& Controller) { Controller.EndPlay(EEndPlayReason::RemovedFromWorld); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossBattleRetryRequestTest, "ProjectRPG.Boss.BattleRetryRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossBattleRetryRequestTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Retry request world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Controller = World->SpawnActor<ARPGPlayerController>();
	auto* Battle = World->SpawnActor<ARPGBossBattleGameState>();
	if (!TestNotNull(TEXT("Retry controller"), Controller) || !TestNotNull(TEXT("Retry GameState"), Battle)) return false;
	Controller->SetAsLocalPlayerController(); // No real viewport or network in this isolated request-state test.
	World->SetGameState(Battle);
	// Inject the received snapshot only; encounter lifecycle is covered by BattleRetry.
	auto* SnapshotProperty = FindFProperty<FStructProperty>(Battle->GetClass(), TEXT("BattleSnapshot"));
	if (!TestNotNull(TEXT("Replicated snapshot property"), SnapshotProperty)) return false;
	auto* Snapshot = SnapshotProperty->ContainerPtrToValuePtr<FRPGBossBattleSnapshot>(Battle);
	Snapshot->BattleState = EBossBattleState::Defeated;
	Snapshot->Revision = 5;
	TestTrue(TEXT("Standalone result remains retryable without backend login"), Controller->CanRequestBossBattleRestart());
	using Access = FRPGBossBattleRetryRequestTestAccess;
	auto Arm = [&](uint32 Id = 10, double Deadline = FPlatformTime::Seconds() + 30.0)
	{
		Access::Arm(*Controller, Id, World, Battle->GetBattleSnapshot().Revision, Deadline);
	};
	Arm();
	TestTrue(TEXT("Current request is pending"), Controller->IsBossBattleRestartPending());
	TestFalse(TEXT("Pending request disables legacy and native retry"), Controller->CanRequestBossBattleRestart());
	TestFalse(TEXT("Duplicate UI dispatch is rejected before sending an RPC"), Controller->TryRequestBossBattleRestart());
	Access::Reply(*Controller, 9, false);
	TestTrue(TEXT("Old rejection cannot unlock current request"), Controller->IsBossBattleRestartPending());
	Access::Reply(*Controller, 10, true);
	TestTrue(TEXT("Acceptance waits for server snapshot, not local transition"), Controller->IsBossBattleRestartPending());
	Battle->PrepareNextBattle(); Access::Update(*Controller);
	TestFalse(TEXT("New replicated revision releases pending request"), Controller->IsBossBattleRestartPending());
	TestTrue(TEXT("Successful transition clears error"), Controller->GetBossBattleRestartErrorCode().IsEmpty());
	Snapshot->BattleState = EBossBattleState::Defeated; ++Snapshot->Revision; Arm(11);
	Access::Reply(*Controller, 10, false);
	TestTrue(TEXT("Previous encounter reply cannot affect next result"), Controller->IsBossBattleRestartPending());
	Access::Reply(*Controller, 11, false);
	TestFalse(TEXT("Matching rejection releases lock"), Controller->IsBossBattleRestartPending());
	TestEqual(TEXT("Rejection exposes a safe stable code"), Controller->GetBossBattleRestartErrorCode(), FString(TEXT("boss_retry_rejected")));
	TestTrue(TEXT("Rejected request is retryable"), Controller->CanRequestBossBattleRestart());
	Arm(12, FPlatformTime::Seconds() - 1.0); Access::Update(*Controller);
	TestFalse(TEXT("Timeout cannot leave the UI locked"), Controller->IsBossBattleRestartPending());
	TestEqual(TEXT("Timeout is not treated as success"), Controller->GetBossBattleRestartErrorCode(), FString(TEXT("boss_retry_timed_out")));
	Arm(13); Access::Reply(*Controller, 12, false);
	TestTrue(TEXT("Late timed-out reply cannot cancel a new request"), Controller->IsBossBattleRestartPending());
	Access::Arm(*Controller, 14, nullptr, Battle->GetBattleSnapshot().Revision, FPlatformTime::Seconds() + 30.0);
	Access::Update(*Controller);
	TestFalse(TEXT("Changed world releases stale pending request"), Controller->IsBossBattleRestartPending());
	Arm(15); Access::End(*Controller);
	TestFalse(TEXT("EndPlay clears pending state"), Controller->IsBossBattleRestartPending());
	return true;
}
#endif
