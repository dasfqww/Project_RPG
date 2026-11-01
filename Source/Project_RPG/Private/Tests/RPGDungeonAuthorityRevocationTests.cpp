#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Misc/AutomationTest.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGSecurityTelemetrySubsystem.h"
#include "TimerManager.h"
#include "UObject/Package.h"

// Native fixture only: production guards/callbacks, no HTTP dispatch, database,
// receipt polling, gameplay cancellation, connection ejection, or Steam bypass.
struct FRPGDungeonAuthorityRevocationTestAccess
{
	static uint64 WalletGeneration(const ARPGPlayerState* State) { return State->CurrencyWalletRequestGeneration; }
	static bool WalletTimerActive(ARPGPlayerState* State)
	{ return State->GetWorldTimerManager().TimerExists(State->CurrencyWalletRefreshTimerHandle); }
	static void WalletReply(ARPGPlayerState* State, FRPGEconomyWalletResult Result,
		const FString& Character, const FString& Steam, const FString& Session, uint64 Generation)
	{ State->HandleCurrencyWalletLoaded(MoveTemp(Result), Character, Steam, Session, Generation); }
	static uint32 LoadGeneration(const URPGInventoryProjectionComponent* Projection) { return Projection->ActiveLoadGeneration; }
	static void SeedPending(URPGItemCommandComponent* Commands, const FRPGItemBackendCommitResult& Result)
	{
		Commands->InFlightRequestIds.Add(Result.RequestId);
		Commands->PendingFirstCommitEffects.Add(Result.RequestId, Result);
	}
	static bool HasPendingEffect(const URPGItemCommandComponent* Commands) { return !Commands->PendingFirstCommitEffects.IsEmpty(); }
	static void ConfigureTelemetry(URPGSecurityTelemetrySubsystem* Telemetry, const FString& Session)
	{
		Telemetry->bAvailable = true;
		Telemetry->bPersistToDisk = false;
		Telemetry->DungeonSessionId = Session;
	}
	static FRPGSecurityTelemetryQueue& Queue(URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->Queue; }
	static FHttpRequestPtr ReserveRequest(URPGSecurityTelemetrySubsystem* Telemetry, int32 Size)
	{
		Telemetry->Queue.BeginBatch(Size);
		auto Request = FHttpModule::Get().CreateRequest();
		Request->OnProcessRequestComplete().BindUObject(Telemetry,
			&URPGSecurityTelemetrySubsystem::HandleRequestComplete);
		Telemetry->ActiveRequest = Request;
		return Request;
	}
	static void FailPersistence(URPGSecurityTelemetrySubsystem* Telemetry, bool bFail)
	{ Telemetry->bPersistToDisk = bFail; Telemetry->DeadLetterPath.Reset(); }
	static bool HasRequest(const URPGSecurityTelemetrySubsystem* Telemetry) { return Telemetry->ActiveRequest.IsValid(); }
};

namespace RPGDungeonAuthorityRevocationTests
{
	const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
	const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
	const FString OtherCharacter = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003");
	const FString OtherSession = TEXT("10203040-5060-7080-90a0-b0c0d0e0f004");
	const FString Steam = TEXT("76561198000000001");
	const FString OtherSteam = TEXT("76561198000000002");
	FRPGItemOwnerRef Owner(const FString& Id = Character)
	{ FRPGItemOwnerRef Result; Result.Type = ERPGItemOwnerType::Character; Result.OwnerId = Id; return Result; }
	FRPGItemRecord Record(ERPGItemContainerType Container = ERPGItemContainerType::Inventory)
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 42, 1, {}, {}, State);
		FRPGItemLocation Location;
		Location.ContainerType = Container;
		Location.ContainerId = Container == ERPGItemContainerType::Mail ? FGuid::NewGuid().ToString() : Character;
		Location.SlotIndex = 0;
		FRPGItemRecord Result;
		FRPGItemRecord::TryRestore(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("RevocationPotion")),
			1, Owner(), Location, State, 1, ERPGItemLifecycleState::Active, {}, Result);
		return Result;
	}
	FRPGEconomyWalletResult Wallet()
	{
		FRPGEconomyWalletResult Result;
		Result.Status = ERPGEconomyBackendStatus::Succeeded;
		Result.AccountId = Steam;
		Result.CharacterId = Character;
		Result.RosterId = FGuid::NewGuid().ToString();
		FRPGCurrencyBalance Balance;
		Balance.CurrencyCode = TEXT("RevocationGold"); Balance.Scope = ERPGCurrencyScope::Character;
		Balance.OwnerId = Character; Balance.Balance = 42; Balance.MaxBalance = 1000; Balance.Revision = 1;
		Result.Balances.Add(Balance);
		return Result;
	}
	FRPGSecurityTelemetryEvent Event(const FString& Id = Character, const FString& Account = Steam)
	{
		FRPGSecurityTelemetryEvent Result;
		Result.EventId = FGuid::NewGuid(); Result.CharacterId = Id; Result.SteamId = Account;
		Result.Type = ERPGSecurityViolationType::InvalidCombatHit;
		Result.Severity = ERPGSecurityViolationSeverity::High;
		Result.Score = 1; Result.RiskAfter = 2; Result.ServerTimeSeconds = 3;
		return Result;
	}
	struct FFixture
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		APlayerController* Controller = nullptr;
		ARPGPlayerState* State = nullptr;
		URPGInventoryProjectionComponent* Projection = nullptr;
		URPGItemCommandComponent* Commands = nullptr;
		FFixture()
		{
			if (!World) return;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Controller = World->SpawnActor<APlayerController>();
			State = World->SpawnActor<ARPGPlayerState>();
			if (!Controller || !State) return;
			Controller->SetPlayerState(State); State->SetOwner(Controller);
			State->SetAuthenticatedIdentity(Character, Steam, Session);
			Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
			Controller->AddInstanceComponent(Projection); Projection->RegisterComponent();
			Commands = NewObject<URPGItemCommandComponent>(Controller);
			Controller->AddInstanceComponent(Commands); Commands->RegisterComponent();
		}
		~FFixture()
		{
			if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
		bool Revoke()
		{
			// Passing references to the state itself must not lose the original
			// scope when the implementation clears its fields.
			return State->RevokeAuthenticatedIdentity(State->GetBackendDungeonSessionId(),
				State->GetBackendCharacterId(), State->GetAuthenticatedSteamId());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonIdentityRevocationTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.Identity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonIdentityRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonAuthorityRevocationTests;
	FFixture F;
	if (!TestNotNull(TEXT("PlayerState"), F.State)) return false;
	F.State->SetAuthenticatedIdentity(Character, Steam, Session, 7);
	TestEqual(TEXT("Consumed-ticket generation stays on the server PlayerState"),
		F.State->GetBackendConnectionGeneration(), int64(7));
	TestFalse(TEXT("Different dungeon cannot revoke"), F.State->RevokeAuthenticatedIdentity(OtherSession, Character, Steam));
	TestFalse(TEXT("Different character cannot revoke"), F.State->RevokeAuthenticatedIdentity(Session, OtherCharacter, Steam));
	TestFalse(TEXT("Different account cannot revoke"), F.State->RevokeAuthenticatedIdentity(Session, Character, OtherSteam));
	F.State->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot revoke"), F.Revoke());
	F.State->SetRole(ROLE_Authority);
	TestTrue(TEXT("Rejected attempts preserve identity"), F.State->HasAuthenticatedCharacter());
	auto* Remaining = F.World->SpawnActor<ARPGPlayerState>();
	Remaining->SetAuthenticatedIdentity(OtherCharacter, OtherSteam, Session);
	bool bObserverSawBarrier = false;
	const auto Handle = F.Projection->OnAuthoritativeRecordsChanged.AddLambda([&]
	{
		bObserverSawBarrier = F.State->IsAuthenticatedIdentityRevoked() && !F.State->HasAuthenticatedCharacter()
			&& !F.Commands->IsAuthenticatedItemOwner(Owner()) && !F.Projection->GetAuthoritativeRecordSource();
		F.State->SetAuthenticatedIdentity(Character, Steam, Session); // Re-entrant grant must fail.
	});
	TestTrue(TEXT("Exact server identity revoked"), F.Revoke());
	TestTrue(TEXT("Observers see all local barriers before notifications"), bObserverSawBarrier);
	F.Projection->OnAuthoritativeRecordsChanged.Remove(Handle);
	TestTrue(TEXT("Identity fields cleared"), F.State->GetBackendCharacterId().IsEmpty()
		&& F.State->GetAuthenticatedSteamId().IsEmpty() && F.State->GetBackendDungeonSessionId().IsEmpty());
	TestEqual(TEXT("Revocation also clears connection generation"),
		F.State->GetBackendConnectionGeneration(), int64(0));
	TestFalse(TEXT("Duplicate receipt has no second effect"), F.State->RevokeAuthenticatedIdentity(Session, Character, Steam));
	F.State->SetAuthenticatedIdentity(Character, Steam, OtherSession);
	TestFalse(TEXT("Revoked PlayerState cannot be reused for another session"), F.State->HasAuthenticatedCharacter());
	TestTrue(TEXT("Remaining party is untouched"), Remaining->HasAuthenticatedCharacter());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonItemRevocationTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.Items",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonItemRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonAuthorityRevocationTests;
	FFixture F;
	if (!TestNotNull(TEXT("Projection"), F.Projection)) return false;
	const auto Item = Record(), Mail = Record(ERPGItemContainerType::Mail);
	TestTrue(TEXT("Owned inventory/mail seed"), F.Projection->ApplyAuthoritativeRecords(Owner(), { Item, Mail }));
	TestEqual(TEXT("Inventory seeded"), F.Projection->GetProjectedItems().Num(), 1);
	TestEqual(TEXT("Mail seeded"), F.Projection->GetMailItems().Num(), 1);
	FRPGItemBackendCommitResult Commit;
	Commit.Status = ERPGItemBackendStatus::Succeeded; Commit.Actor = Owner();
	Commit.RequestId = FGuid::NewGuid(); Commit.Operation = TEXT("Item.Consume"); Commit.Records.Add(Item);
	FRPGDungeonAuthorityRevocationTestAccess::SeedPending(F.Commands, Commit);
	const uint64 Generation = F.Projection->GetAuthoritativeSnapshotGeneration();
	const uint32 LoadGeneration = FRPGDungeonAuthorityRevocationTestAccess::LoadGeneration(F.Projection);
	F.Commands->InvalidateForRevokedIdentity(*F.State);
	F.Projection->InvalidateForRevokedIdentity(*F.State);
	TestTrue(TEXT("Unrevoked identity cannot clear component state"), F.Commands->HasInFlightCommands()
		&& F.Projection->GetAuthoritativeRecordSource());
	TestTrue(TEXT("Production state revocation"), F.Revoke());
	TestFalse(TEXT("Pending requests cleared"), F.Commands->HasInFlightCommands());
	TestFalse(TEXT("Pending first-commit effects cleared"), FRPGDungeonAuthorityRevocationTestAccess::HasPendingEffect(F.Commands));
	TestTrue(TEXT("Full loads and reward snapshots invalidated"),
		F.Projection->GetAuthoritativeSnapshotGeneration() > Generation
		&& FRPGDungeonAuthorityRevocationTestAccess::LoadGeneration(F.Projection) > LoadGeneration);
	TestTrue(TEXT("No authoritative item cache exposed"), !F.Projection->GetAuthoritativeRecordSource());
	TestTrue(TEXT("Owner inventory and mail projections cleared"), F.Projection->GetProjectedItems().IsEmpty()
		&& F.Projection->GetMailItems().IsEmpty());
	FString Error;
	TestFalse(TEXT("Late full load cannot restore inventory"), F.Projection->ApplyAuthoritativeRecords(Owner(), { Item }));
	TestFalse(TEXT("Late mutation cannot restore inventory"), F.Projection->ApplyAuthoritativeMutationRecords(Owner(), { Item }));
	TestFalse(TEXT("Late reward refresh cannot restore inventory"), F.Projection->ApplyCompletedRewardSnapshot(Owner(), { Item }, Generation));
	TestFalse(TEXT("Late commit sink rejected"), F.Commands->ApplyCommitAndReconcile(Owner(), Commit, Error));
	TestFalse(TEXT("Late first-commit effect rejected"), F.Commands->EnqueueFirstCommitEffect(Commit, Error));
	TestFalse(TEXT("New backend inventory load rejected"), F.Projection->LoadAuthenticatedCharacterItems());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonWalletRevocationTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.Wallet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonWalletRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonAuthorityRevocationTests;
	FFixture F;
	if (!TestNotNull(TEXT("PlayerState"), F.State)) return false;
	const auto Result = Wallet();
	uint64 Generation = FRPGDungeonAuthorityRevocationTestAccess::WalletGeneration(F.State);
	TestTrue(TEXT("Periodic wallet timer initially active"), FRPGDungeonAuthorityRevocationTestAccess::WalletTimerActive(F.State));
	FRPGDungeonAuthorityRevocationTestAccess::WalletReply(F.State, Result, Character, Steam, Session, Generation);
	TestEqual(TEXT("Current production callback applies wallet"), F.State->GetCurrencyBalance(TEXT("RevocationGold")), int64(42));
	F.State->SetAuthenticatedIdentity(Character, Steam, OtherSession);
	const uint64 NewGeneration = FRPGDungeonAuthorityRevocationTestAccess::WalletGeneration(F.State);
	TestTrue(TEXT("Identity switch invalidates requests even without a backend gateway"), NewGeneration != Generation);
	auto Different = Result; Different.Balances[0].Balance = 77;
	FRPGDungeonAuthorityRevocationTestAccess::WalletReply(F.State, Different, Character, Steam, Session, NewGeneration);
	TestEqual(TEXT("Old session reply cannot apply at current generation"), F.State->GetCurrencyBalance(TEXT("RevocationGold")), int64(42));
	F.State->SetAuthenticatedIdentity(Character, Steam, Session);
	FRPGDungeonRewardDeliveryNotice Notice;
	Notice.DungeonSessionId = Session; Notice.bApplied = true;
	TestTrue(TEXT("Completed reward notice seed"), F.State->ApplyCompletedRewardWallet(Result, Notice));
	Generation = FRPGDungeonAuthorityRevocationTestAccess::WalletGeneration(F.State);
	TestTrue(TEXT("Revoke"), F.Revoke());
	TestTrue(TEXT("Wallet and notice cleared"), F.State->GetCurrencyBalances().IsEmpty()
		&& F.State->GetBackendRosterId().IsEmpty() && !F.State->GetRewardDeliveryNotice().bApplied);
	TestFalse(TEXT("Wallet timer stopped"), FRPGDungeonAuthorityRevocationTestAccess::WalletTimerActive(F.State));
	FRPGDungeonAuthorityRevocationTestAccess::WalletReply(F.State, Result, Character, Steam, Session, Generation);
	TestTrue(TEXT("Late successful HTTP callback cannot restore wallet"), F.State->GetCurrencyBalances().IsEmpty());
	TestFalse(TEXT("Late completed reward cannot restore wallet/notice"), F.State->ApplyCompletedRewardWallet(Result, Notice));
	const uint64 RevokedGeneration = FRPGDungeonAuthorityRevocationTestAccess::WalletGeneration(F.State);
	F.State->RefreshCurrencyWallet();
	TestEqual(TEXT("Revoked identity cannot start new wallet request"),
		FRPGDungeonAuthorityRevocationTestAccess::WalletGeneration(F.State), RevokedGeneration);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonTelemetryRevocationTest,
	"ProjectRPG.Online.DungeonAuthorityRevocation.Telemetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonTelemetryRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonAuthorityRevocationTests;
	using Access = FRPGDungeonAuthorityRevocationTestAccess;
	auto* Instance = NewObject<UGameInstance>();
	auto* Telemetry = NewObject<URPGSecurityTelemetrySubsystem>(Instance);
	Access::ConfigureTelemetry(Telemetry, Session);
	auto& Queue = Access::Queue(Telemetry);
	const auto Removed = Event(), Remaining = Event(OtherCharacter, OtherSteam), SameCharacterOtherAccount = Event(Character, OtherSteam);
	Queue.Enqueue(Removed); Queue.Enqueue(Remaining); Queue.Enqueue(SameCharacterOtherAccount);
	auto Request = Access::ReserveRequest(Telemetry, 2);
	auto Late = Request->OnProcessRequestComplete();
	TestFalse(TEXT("Foreign session cannot quarantine"), Telemetry->QuarantineRevokedDungeonMember(OtherSession, Character, Steam));
	TestTrue(TEXT("Foreign scope preserves in-flight batch"), Access::HasRequest(Telemetry) && Queue.GetReservedCount() == 2);
	TestTrue(TEXT("Exact member quarantined"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestFalse(TEXT("Mixed in-flight request canceled locally"), Access::HasRequest(Telemetry));
	TestEqual(TEXT("Only removed member evidence leaves delivery queue"), Queue.Num(), 2);
	TestTrue(TEXT("Remaining evidence and stable event ID preserved"), Queue.GetEvents()[0].EventId == Remaining.EventId);
	TestTrue(TEXT("Same character under another account is not blanket removed"), Queue.GetEvents()[1].EventId == SameCharacterOtherAccount.EventId);
	auto Next = Access::ReserveRequest(Telemetry, 2);
	Late.Execute(Request, FHttpResponsePtr(), false);
	TestTrue(TEXT("Copied canceled callback cannot release new request/batch"), Access::HasRequest(Telemetry)
		&& Queue.GetReservedCount() == 2 && Queue.Num() == 2);
	TestFalse(TEXT("Conflicting account cannot overwrite immutable revoke"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, OtherSteam));
	TestTrue(TEXT("Duplicate revoke preserves remaining-only active request"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam)
		&& Access::HasRequest(Telemetry) && Queue.GetReservedCount() == 2);
	Telemetry->Deinitialize();

	// Failed durable quarantine must preserve evidence and block submission.
	Telemetry = NewObject<URPGSecurityTelemetrySubsystem>(Instance);
	Access::ConfigureTelemetry(Telemetry, Session); Access::FailPersistence(Telemetry, true);
	Access::Queue(Telemetry).Enqueue(Event());
	AddExpectedError(TEXT("Revoked member audit quarantine could not be persisted; delivery paused."),
		EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("Persistence failure retains evidence"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestEqual(TEXT("Original event still queued"), Telemetry->GetQueuedEventCount(), 1);
	TestFalse(TEXT("Failed quarantine prevents a new POST"), Telemetry->FlushNow());
	TestFalse(TEXT("No request dispatched"), Access::HasRequest(Telemetry));
	Access::FailPersistence(Telemetry, false);
	TestTrue(TEXT("Retry can complete quarantine"), Telemetry->QuarantineRevokedDungeonMember(Session, Character, Steam));
	TestEqual(TEXT("Quarantined queue drained"), Telemetry->GetQueuedEventCount(), 0);
	Telemetry->Deinitialize();
	return true;
}
#endif
