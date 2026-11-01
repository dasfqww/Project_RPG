#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGDungeonEjectionTestFixture.h"
#include "Tests/RPGBossBattleRetryTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "Component/RPGPlayerMovementComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/ChildConnection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "IpNetDriver.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "UObject/Package.h"

struct FRPGDungeonEjectionTestAccess
{
	static FRPGDungeonMembershipMonitor& Monitor(ARPGGameModeBase& Mode) { return Mode.DungeonMembershipMonitor; }
	static bool Request(ARPGGameModeBase& Mode, APlayerController* Controller) { return Mode.RequestDungeonMemberEjection(Controller); }
	static bool Complete(ARPGGameModeBase& Mode, uint64 Serial, const FRPGDungeonMembershipSnapshotResult& Result)
	{ return Mode.CompleteDungeonMembershipRead(Serial, Result); }
	static int32 Sweep(ARPGGameModeBase& Mode) { return Mode.ApplyCachedDungeonRevocations(); }
	static bool RequestPending(ARPGGameModeBase& Mode, APlayerController* Controller)
	{ return Mode.RequestPendingDungeonAdmissionEjection(Controller); }
	static void QueuePending(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection,
		const FString& SessionId, const FString& CharacterId, const FString& SteamId, int64 Generation = 42)
	{
		auto& Pending = Mode.AdmissionsAwaitingBackend.Add(Controller);
		Pending.Admission = {SessionId, CharacterId, SteamId, Generation};
		Pending.Connection = Connection; Pending.PlayerState = Controller->GetPlayerState<ARPGPlayerState>();
	}
	static bool HasPending(ARPGGameModeBase& Mode, APlayerController* Controller)
	{ return Mode.AdmissionsAwaitingBackend.Contains(TWeakObjectPtr<APlayerController>(Controller)); }
	static void ReplyPending(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection,
		const FString& SessionId, const FString& CharacterId, const FString& SteamId)
	{
		ARPGGameModeBase::FValidatedAdmission Expected{SessionId, CharacterId, SteamId, 42};
		Mode.CompleteDungeonAdmission(Controller, Connection, Expected, true, 200);
	}
	static FString Reason(ARPGGameModeBase& Mode, const FString& SessionId, const FString& CharacterId, const FString& SteamId)
	{ return Mode.GetDungeonAdmissionRevocationReason(SessionId, CharacterId, SteamId); }
	static void Confirm(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection,
		const FString& SessionId, const FString& CharacterId, const FString& SteamId)
	{
		ARPGGameModeBase::FValidatedAdmission Expected{SessionId, CharacterId, SteamId, 42};
		auto& Pending = Mode.AdmissionsAwaitingBackend.Add(Controller);
		Pending.Admission = Expected; Pending.Connection = Connection;
		Pending.PlayerState = Controller->GetPlayerState<ARPGPlayerState>();
		Mode.CompleteDungeonAdmission(Controller, Connection, Expected, true, 200);
	}
	static bool Activated(ARPGGameModeBase& Mode, APlayerController* Controller)
	{ return Mode.ActivatedAdmissions.Contains(TWeakObjectPtr<APlayerController>(Controller)); }
};

namespace RPGDungeonEjectionTests
{
	const FString Session = TEXT("20304050-6070-8090-a0b0-c0d0e0f00102");
	const FString Server = TEXT("native-ejection-server");
	struct FMember
	{
		APlayerController* Controller = nullptr;
		ARPGPlayerState* State = nullptr;
		ARPGBossRetryTestPlayer* Pawn = nullptr;
		URPGDungeonEjectionTestConnection* Connection = nullptr;
	};
	struct FFixture
	{
		UWorld* World = nullptr;
		ARPGDungeonEjectionTestGameMode* Mode = nullptr;
		UIpNetDriver* Driver = nullptr;
		FMember First, Remaining;
		bool bUseProductionController = false;
		FFixture(bool bProductionController = false) : bUseProductionController(bProductionController)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			if (!World) return;
			UGameInstance* Instance = NewObject<UGameInstance>();
			auto& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World); Context.OwningGameInstance = Instance;
			World->SetGameInstance(Instance);
			FURL Url;
			Url.AddOption(TEXT("game=/Script/Project_RPG.RPGDungeonEjectionTestGameMode"));
			if (!World->SetGameMode(Url)) return;
			Mode = Cast<ARPGDungeonEjectionTestGameMode>(World->GetAuthGameMode());
			if (!Mode) return;
			World->InitializeActorsForPlay(Url); World->CreateAISystem(); World->BeginPlay();
			World->GetWorldSettings()->NotifyBeginPlay();
			Driver = NewObject<UIpNetDriver>(); Driver->SetWorld(World); World->SetNetDriver(Driver);
			First = AddMember(TEXT("76561198000000001"));
			Remaining = AddMember(TEXT("76561198000000002"));
			FRPGDungeonEjectionTestAccess::Monitor(*Mode).Configure(Session, Server);
		}
		URPGDungeonEjectionTestConnection* AddConnection(APlayerController* Controller)
		{
			auto* Connection = NewObject<URPGDungeonEjectionTestConnection>(Driver);
			Connection->InitializeNativeConnection(*Driver);
			Connection->PlayerController = Controller; Connection->OwningActor = Controller;
			Driver->ClientConnections.Add(Connection);
			return Connection;
		}
		FMember AddMember(const FString& Steam)
		{
			FMember Member;
			Member.Controller = World->SpawnActor<APlayerController>(bUseProductionController
				? ARPGPlayerController::StaticClass() : ARPGBossThreatProbeController::StaticClass());
			Member.State = World->SpawnActor<ARPGPlayerState>();
			Member.Controller->SetPlayerState(Member.State); Member.State->SetOwner(Member.Controller);
			Member.State->SetAuthenticatedIdentity(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens), Steam, Session);
			Member.Pawn = World->SpawnActor<ARPGBossRetryTestPlayer>(); Member.Controller->Possess(Member.Pawn);
			Member.Connection = AddConnection(Member.Controller);
			Member.Controller->Player = Member.Connection; Member.Controller->NetConnection = Member.Connection;
			Mode->AddTestParticipant(Member.Controller);
			return Member;
		}
		FMember AddPendingMember()
		{
			FMember Member;
			Member.Controller = World->SpawnActor<APlayerController>();
			Member.State = World->SpawnActor<ARPGPlayerState>();
			Member.Controller->SetPlayerState(Member.State); Member.State->SetOwner(Member.Controller);
			Member.Connection = AddConnection(Member.Controller);
			Member.Controller->Player = Member.Connection; Member.Controller->NetConnection = Member.Connection;
			// No identity, Pawn or encounter participation before admission confirmation.
			return Member;
		}
		FRPGDungeonMembershipSnapshotResult MakeReceipt(const FString& CharacterId, const FString& SteamId, bool bSuccess = true)
		{
			FRPGDungeonMembershipSnapshotResult Result;
			Result.bSuccess = bSuccess; Result.HttpStatus = bSuccess ? 200 : 500;
			Result.Snapshot.DungeonSessionId = Session; Result.Snapshot.ServerId = Server;
			Result.Snapshot.State = TEXT("InProgress");
			FRPGDungeonMembershipAbandonment& Entry = Result.Snapshot.Abandonments.AddDefaulted_GetRef();
			Entry.SteamId = SteamId; Entry.Receipt.DungeonSessionId = Session; Entry.Receipt.CharacterId = CharacterId;
			Entry.Receipt.AbandonedAt = TEXT("2026-10-11T00:00:00Z");
			Entry.Receipt.SessionStateAtAbandonment = TEXT("InProgress"); Entry.Receipt.bRewardExcluded = true;
			return Result;
		}
		bool RecordReceipt(const FString& CharacterId, const FString& SteamId, bool bSuccess = true)
		{
			const auto Result = MakeReceipt(CharacterId, SteamId, bSuccess);
			uint64 Serial = 0;
			auto& Monitor = FRPGDungeonEjectionTestAccess::Monitor(*Mode);
			return Monitor.BeginRead(Serial) && Monitor.FinishRead(Serial, Result);
		}
		FRPGDungeonMembershipSnapshotResult MakeExpiry(const FString& CharacterId, const FString& SteamId)
		{
			auto Result = MakeReceipt(CharacterId, SteamId);
			Result.Snapshot.Abandonments.Reset();
			auto& Entry = Result.Snapshot.DisconnectionExpulsions.AddDefaulted_GetRef();
			Entry.CharacterId = CharacterId; Entry.SteamId = SteamId;
			Entry.DisconnectedAt = TEXT("2026-10-17T00:00:00Z");
			Entry.ExpiredAt = TEXT("2026-10-17T00:10:00Z");
			Entry.ProcessedAt = TEXT("2026-10-17T00:10:01Z"); Entry.bRewardExcluded = true;
			return Result;
		}
		~FFixture()
		{
			// Only fixture-owned connections. Detach actors before world destruction so
			// teardown cannot dispatch fabricated packets or close another world's driver.
			if (Driver)
			{
				for (UNetConnection* Connection : Driver->ClientConnections)
				{
					if (APlayerController* Controller = Connection->PlayerController;
						IsValid(Controller) && Controller->Player == Connection)
					{ Controller->Player = nullptr; Controller->NetConnection = nullptr; }
					Connection->OwningActor = nullptr; Connection->PlayerController = nullptr; Connection->Driver = nullptr;
				}
				Driver->ClientConnections.Reset(); World->SetNetDriver(nullptr); Driver->SetWorld(nullptr);
			}
			if (World) { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPendingAdmissionRevocationTest,
	"ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPendingAdmissionRevocationTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	using Access = FRPGDungeonEjectionTestAccess;
	for (bool bExpiry : {false, true})
	{
		FFixture F;
		if (!TestNotNull(TEXT("Authoritative pending admission mode"), F.Mode)) return false;
		auto Target = F.AddPendingMember();
		const FString Character = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		const FString Steam = TEXT("76561198000000003");
		Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, Session, Character, Steam);
		uint64 Serial = 0;
		TestTrue(TEXT("Receipt read begins"), Access::Monitor(*F.Mode).BeginRead(Serial));
		TestTrue(TEXT("Confirmed receipt completion handles pending connection"), Access::Complete(*F.Mode, Serial,
			bExpiry ? F.MakeExpiry(Character, Steam) : F.MakeReceipt(Character, Steam)));
		TestEqual(TEXT("Exact pending connection closes without waiting for confirmation"), Target.Connection->GetConnectionState(), USOCK_Closed);
		TestFalse(TEXT("Pending waiter removed before late callback"), Access::HasPending(*F.Mode, Target.Controller));
		Access::ReplyPending(*F.Mode, Target.Controller, Target.Connection, Session, Character, Steam);
		TestFalse(TEXT("Late success cannot publish identity"), Target.State->HasAuthenticatedCharacter());
		TestFalse(TEXT("Late success cannot publish activation"), Access::Activated(*F.Mode, Target.Controller));
		TestNull(TEXT("Late success cannot spawn"), Target.Controller->GetPawn());
		TestEqual(TEXT("Duplicate sweep is inert"), Access::Sweep(*F.Mode), 0);
		TestEqual(TEXT("No manual Logout"), F.Mode->LogoutCount, 0);
		TestEqual(TEXT("Existing party participation unchanged"), F.Mode->GetTestParticipantCount(), 2);
		TestEqual(TEXT("Existing first member stays open"), F.First.Connection->GetConnectionState(), USOCK_Open);
		TestEqual(TEXT("Existing remaining member stays open"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
		TestTrue(TEXT("Remaining identities and Pawns preserved"), F.First.State->HasAuthenticatedCharacter()
			&& F.Remaining.State->HasAuthenticatedCharacter() && IsValid(F.First.Pawn) && IsValid(F.Remaining.Pawn));
		Target.Connection->CleanUp();
		TestEqual(TEXT("Engine owns Logout"), F.Mode->LogoutCount, 1);
		TestEqual(TEXT("Only pending connection removed"), F.Driver->ClientConnections.Num(), 2);
		TestEqual(TEXT("No socket packet injection"), Target.Connection->SendCalls, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPendingAdmissionRevocationGuardsTest,
	"ProjectRPG.Online.DungeonEjection.PendingAdmissionRevocationGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPendingAdmissionRevocationGuardsTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	using Access = FRPGDungeonEjectionTestAccess;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative guard mode"), F.Mode)) return false;
	auto Target = F.AddPendingMember();
	const FString Character = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	const FString Steam = TEXT("76561198000000003");
	Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, Session, Character, Steam);
	TestFalse(TEXT("Unauthenticated presence is not a receipt"), Access::RequestPending(*F.Mode, Target.Controller));
	TestFalse(TEXT("Failed read supplies no receipt"), F.RecordReceipt(Character, Steam, false));
	TestFalse(TEXT("Failed refresh cannot close a pending socket"), Access::RequestPending(*F.Mode, Target.Controller));
	TestTrue(TEXT("Exact receipt cached"), F.RecordReceipt(Character, Steam));
	const FString OtherSession = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, OtherSession, Character, Steam);
	TestFalse(TEXT("Foreign session rejected"), Access::RequestPending(*F.Mode, Target.Controller));
	Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, Session, Character, TEXT("76561198000000004"));
	TestFalse(TEXT("Foreign account rejected"), Access::RequestPending(*F.Mode, Target.Controller));
	Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, Session, Character, Steam, 0);
	TestFalse(TEXT("Missing consumed generation rejected"), Access::RequestPending(*F.Mode, Target.Controller));
	Access::QueuePending(*F.Mode, Target.Controller, Target.Connection, Session, Character, Steam);
	F.Mode->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client authority cannot close"), Access::RequestPending(*F.Mode, Target.Controller));
	F.Mode->SetRole(ROLE_Authority);
	auto* Replacement = F.AddConnection(Target.Controller);
	Target.Controller->Player = Replacement; Target.Controller->NetConnection = Replacement;
	TestFalse(TEXT("Old waiter cannot close a replacement socket"), Access::RequestPending(*F.Mode, Target.Controller));
	TestEqual(TEXT("Replacement stays open"), Replacement->GetConnectionState(), USOCK_Open);
	Target.Controller->Player = Target.Connection; Target.Controller->NetConnection = Target.Connection;
	Target.Connection->OwningActor = F.Remaining.Controller;
	TestFalse(TEXT("Foreign owning actor rejected"), Access::RequestPending(*F.Mode, Target.Controller));
	Target.Connection->OwningActor = Target.Controller;
	auto* Child = NewObject<UChildConnection>(Target.Connection);
	Target.Connection->Children.Add(Child);
	TestFalse(TEXT("Split-screen peer is not collateral damage"), Access::RequestPending(*F.Mode, Target.Controller));
	Target.Connection->Children.Reset();
	TestTrue(TEXT("Rejected guards retain the original waiter"), Access::HasPending(*F.Mode, Target.Controller));
	TestTrue(TEXT("Exact restored ownership permits receipt-gated close"), Access::RequestPending(*F.Mode, Target.Controller));
	TestEqual(TEXT("Remaining party socket untouched"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEjectionLifecycleTest, "ProjectRPG.Online.DungeonEjection.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonEjectionLifecycleTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F(true);
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode) || !TestNotNull(TEXT("Fixture movement"), F.First.Pawn)) return false;
	ARPGPlayerController* CleanupController = CastChecked<ARPGPlayerController>(F.First.Controller);
	TestNull(TEXT("No cleanup identity outside native callback"), CleanupController->GetConnectionBeingCleanedUp());
	const FVector Location = F.First.Pawn->GetActorLocation();
	UFunction* PackedMove = F.First.Pawn->FindFunction(GET_FUNCTION_NAME_CHECKED(ACharacter, ServerMovePacked));
	if (!TestNotNull(TEXT("Actual inherited packed move RPC"), PackedMove)) return false;
	FCharacterServerMovePackedBits QueuedPayload;
	// The non-revoked member must still reach normal engine decode. This native
	// fixture intentionally has no PackageMap and accepts only this one diagnostic.
	AddExpectedError(TEXT("Failed to find PackageMap for data serialization!"), EAutomationExpectedErrorFlags::Contains, 1);
	F.Remaining.Pawn->ProcessEvent(PackedMove, &QueuedPayload);
	TestTrue(TEXT("Validated native receipt cached"), F.RecordReceipt(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId()));
	TestTrue(TEXT("Production request closes exact connection"), FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller));
	TestTrue(TEXT("Identity revoked before cleanup"), F.First.State->IsAuthenticatedIdentityRevoked());
	TestTrue(TEXT("Pawn/controller retained for deferred engine cleanup"), IsValid(F.First.Pawn) && IsValid(F.First.Controller));
	TestEqual(TEXT("No manual Logout during close"), F.Mode->LogoutCount, 0);
	TestEqual(TEXT("Engine connection closed"), F.First.Connection->GetConnectionState(), USOCK_Closed);
	TestFalse(TEXT("Duplicate request cannot act twice"), FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller));
	// Deliberately has no PackageMap. A revoked raw RPC must return before decode.
	F.First.Pawn->ProcessEvent(PackedMove, &QueuedPayload);
	F.First.Pawn->Jump();
	UFunction* StopJump = F.First.Pawn->FindFunction(GET_FUNCTION_NAME_CHECKED(ACharacter, StopJumping));
	if (!TestNotNull(TEXT("Unrelated native event"), StopJump)) return false;
	F.First.Pawn->ProcessEvent(StopJump, nullptr);
	TestFalse(TEXT("Unrelated event still dispatches on revoked Pawn"), F.First.Pawn->bPressedJump);
	auto* Movement = Cast<URPGPlayerMovementComponent>(F.First.Pawn->GetCharacterMovement());
	if (!TestNotNull(TEXT("Revoked player movement"), Movement)) return false;
	Movement->SetMovementMode(MOVE_Walking); Movement->Velocity = FVector(100.f, 0.f, 0.f); Movement->PerformMovement(.1f);
	TestEqual(TEXT("Queued movement cannot restore authority"), Movement->MovementMode, MOVE_None);
	TestEqual(TEXT("Queued movement cannot move Pawn"), F.First.Pawn->GetActorLocation(), Location);
	TestTrue(TEXT("Remaining member still authoritative"), F.Remaining.State->HasAuthenticatedCharacter() && !IsDungeonGameplayRevoked(F.Remaining.Pawn));
	TestEqual(TEXT("Remaining connection unaffected"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	// Emulate the next net-driver cleanup boundary, using actual engine code.
	F.First.Connection->CleanUp();
	// CleanUp marks the connection as garbage after Logout; compare identity,
	// not Get(), which correctly stops resolving that weak pointer afterwards.
	TestTrue(TEXT("Exact socket available during synchronous Logout"), F.Mode->ObservedCleanupConnection.HasSameIndexAndSerialNumber(TWeakObjectPtr<UNetConnection>(F.First.Connection)));
	TestNull(TEXT("Cleanup identity cleared when callback returns"), CleanupController->GetConnectionBeingCleanedUp());
	TestEqual(TEXT("Normal controller destruction reaches inherited Logout once"), F.Mode->LogoutCount, 1);
	TestTrue(TEXT("Owning actor/Pawn removed only at engine cleanup"), !IsValid(F.First.Controller) && !IsValid(F.First.Pawn));
	TestEqual(TEXT("Only abandoned participant removed"), F.Mode->GetTestParticipantCount(), 1);
	TestEqual(TEXT("Only abandoned retry state removed"), F.Mode->GetTestRetryStateCount(), 1);
	TestTrue(TEXT("Remaining connection stays registered"), F.Driver->ClientConnections.Num() == 1 && F.Driver->ClientConnections.Contains(F.Remaining.Connection));
	TestTrue(TEXT("Remaining Pawn survives Logout"), IsValid(F.Remaining.Pawn) && F.Remaining.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("No socket send in native fixture"), F.First.Connection->SendCalls + F.Remaining.Connection->SendCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEjectionGuardsTest, "ProjectRPG.Online.DungeonEjection.Guards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonEjectionGuardsTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode)) return false;
	auto Request = [&]() { return FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller); };
	const FString Character = F.First.State->GetBackendCharacterId(), Steam = F.First.State->GetAuthenticatedSteamId();
	TestFalse(TEXT("No receipt cannot revoke"), Request());
	TestFalse(TEXT("Failed read cannot add evidence"), F.RecordReceipt(Character, Steam, false));
	TestFalse(TEXT("HTTP failure never ejects"), Request());
	TestTrue(TEXT("Foreign account receipt can be valid for its own scope"), F.RecordReceipt(Character, TEXT("76561198000000003")));
	TestFalse(TEXT("Same character different account never ejects"), Request());
	FRPGDungeonEjectionTestAccess::Monitor(*F.Mode).Configure(Session, Server);
	TestTrue(TEXT("Exact receipt cached"), F.RecordReceipt(Character, Steam));
	F.First.State->SetRole(ROLE_SimulatedProxy); TestFalse(TEXT("Non-authority state rejected"), Request()); F.First.State->SetRole(ROLE_Authority);
	F.First.State->SetOwner(F.Remaining.Controller); TestFalse(TEXT("Mismatched state owner rejected"), Request()); F.First.State->SetOwner(F.First.Controller);
	F.First.State->SetAuthenticatedIdentity(Character, Steam, FGuid::NewGuid().ToString()); TestFalse(TEXT("Other session rejected"), Request()); F.First.State->SetAuthenticatedIdentity(Character, Steam, Session);
	F.First.Connection->PlayerController = F.Remaining.Controller; TestFalse(TEXT("Foreign connection controller rejected"), Request()); F.First.Connection->PlayerController = F.First.Controller;
	F.First.Connection->OwningActor = F.Remaining.Controller; TestFalse(TEXT("Foreign connection actor rejected"), Request()); F.First.Connection->OwningActor = F.First.Controller;
	F.First.Controller->NetConnection = F.Remaining.Connection; TestFalse(TEXT("Replaced connection rejected"), Request()); F.First.Controller->NetConnection = F.First.Connection;
	F.Driver->ServerConnection = F.First.Connection; TestFalse(TEXT("Client-side driver rejected"), Request()); F.Driver->ServerConnection = nullptr;
	F.Driver->ClientConnections.Remove(F.First.Connection); TestFalse(TEXT("Unregistered connection rejected"), Request()); F.Driver->ClientConnections.Add(F.First.Connection);
	UChildConnection* Child = NewObject<UChildConnection>(F.Driver);
	F.First.Connection->Children.Add(Child); TestFalse(TEXT("No collateral split-screen disconnect"), Request()); F.First.Connection->Children.Reset();
	F.First.Controller->Player = Child; F.First.Controller->NetConnection = Child;
	Child->Driver = F.Driver; Child->PlayerController = F.First.Controller; Child->OwningActor = F.First.Controller;
	Child->SetConnectionState(USOCK_Open); F.Driver->ClientConnections.Add(Child);
	TestFalse(TEXT("Child connection cannot revoke"), Request());
	F.Driver->ClientConnections.Remove(Child); Child->Driver = nullptr; Child->PlayerController = nullptr; Child->OwningActor = nullptr;
	F.First.Controller->Player = F.First.Connection; F.First.Controller->NetConnection = F.First.Connection;
	F.First.Connection->SetConnectionState(USOCK_Closed); TestFalse(TEXT("Already closed transport rejected before revocation"), Request()); F.First.Connection->SetConnectionState(USOCK_Open);
	F.World->SetNetDriver(nullptr); TestFalse(TEXT("Different world driver rejected"), Request()); F.World->SetNetDriver(F.Driver);
	TestTrue(TEXT("Rejected guards leave identity intact"), F.First.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("Rejected guards leave transport open"), F.First.Connection->GetConnectionState(), USOCK_Open);
	FRPGDungeonEjectionTestAccess::Monitor(*F.Mode).Reset(); TestFalse(TEXT("Reset scope cannot authorize ejection"), Request());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonEjectionReentryTest, "ProjectRPG.Online.DungeonEjection.Reentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonEjectionReentryTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode)) return false;
	TestTrue(TEXT("Exact receipt cached"), F.RecordReceipt(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId()));
	URPGDungeonEjectionTestConnection* Replacement = F.AddConnection(F.First.Controller);
	F.Mode->ReplaceConnectionController = F.First.Controller; F.Mode->ReplacementConnection = Replacement;
	F.First.State->OnCharacterIdentityChanged.AddDynamic(F.Mode, &ARPGDungeonEjectionTestGameMode::ReplaceConnectionOnIdentityChanged);
	TestFalse(TEXT("Reentrant replacement aborts transport close"), FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller));
	TestTrue(TEXT("Raised identity barrier is not rolled back"), F.First.State->IsAuthenticatedIdentityRevoked());
	TestEqual(TEXT("Replacement connection never closed"), Replacement->GetConnectionState(), USOCK_Open);
	TestEqual(TEXT("Original connection not closed under changed ownership"), F.First.Connection->GetConnectionState(), USOCK_Open);
	TestEqual(TEXT("No Logout performed by aborted close"), F.Mode->LogoutCount, 0);
	TestTrue(TEXT("Pawn retained without granting gameplay"), IsValid(F.First.Pawn) && IsDungeonGameplayRevoked(F.First.Pawn));
	TestTrue(TEXT("Remaining party not revoked"), F.Remaining.State->HasAuthenticatedCharacter());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipEjectionCompletionTest,
	"ProjectRPG.Online.DungeonEjection.MembershipCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonMembershipEjectionCompletionTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode)) return false;
	auto& Monitor = FRPGDungeonEjectionTestAccess::Monitor(*F.Mode);
	const FString Character = F.First.State->GetBackendCharacterId(), Steam = F.First.State->GetAuthenticatedSteamId();
	auto Complete = [&](FRPGDungeonMembershipSnapshotResult Result)
	{
		uint64 Serial = 0;
		if (!Monitor.BeginRead(Serial)) return false;
		return FRPGDungeonEjectionTestAccess::Complete(*F.Mode, Serial, Result);
	};
	TestFalse(TEXT("No read serial cannot trigger a sweep"), FRPGDungeonEjectionTestAccess::Complete(*F.Mode, 0, F.MakeReceipt(Character, Steam)));
	TestFalse(TEXT("Failed HTTP result cannot revoke"), Complete(F.MakeReceipt(Character, Steam, false)));
	for (const int32 Status : { 401, 403, 404, 409, 429, 500 })
	{
		auto Rejected = F.MakeReceipt(Character, Steam); Rejected.HttpStatus = Status;
		TestFalse(TEXT("Non-200 status cannot authorize even a populated result"), Complete(Rejected));
	}
	auto WrongSession = F.MakeReceipt(Character, Steam);
	WrongSession.Snapshot.DungeonSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	TestFalse(TEXT("Foreign session response cannot authorize"), Complete(WrongSession));
	auto WrongServer = F.MakeReceipt(Character, Steam); WrongServer.Snapshot.ServerId = TEXT("foreign-server");
	TestFalse(TEXT("Foreign assignment response cannot authorize"), Complete(WrongServer));
	auto Missing = F.MakeReceipt(Character, Steam); Missing.Snapshot.Abandonments.Reset();
	TestTrue(TEXT("Missing member/receipt is a valid non-ejecting read"), Complete(Missing));
	TestTrue(TEXT("Foreign account receipt cannot eject current member"), Complete(F.MakeReceipt(Character, TEXT("76561198000000003"))));
	TestTrue(TEXT("All non-ejecting reads preserve both identities"), F.First.State->HasAuthenticatedCharacter() && F.Remaining.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("No connection closed without exact receipt"), F.First.Connection->GetConnectionState(), USOCK_Open);
	Monitor.Configure(Session, Server);
	auto Inconsistent = F.MakeReceipt(Character, Steam);
	auto& Invalid = Inconsistent.Snapshot.Abandonments.AddDefaulted_GetRef();
	Invalid.SteamId = TEXT("not-a-steam-id");
	TestFalse(TEXT("Partial invalid snapshot is rejected atomically"), Complete(Inconsistent));
	TestFalse(TEXT("Rejected snapshot never caches first valid entry"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	TestTrue(TEXT("Exact valid completion automatically closes target"), Complete(F.MakeReceipt(Character, Steam)));
	TestTrue(TEXT("Authority revoked before cleanup"), F.First.State->IsAuthenticatedIdentityRevoked());
	TestEqual(TEXT("Only target connection closed"), F.First.Connection->GetConnectionState(), USOCK_Closed);
	TestEqual(TEXT("Remaining connection stays open"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	TestTrue(TEXT("Actors retained until engine cleanup"), IsValid(F.First.Controller) && IsValid(F.First.Pawn));
	TestEqual(TEXT("No manual Logout in callback"), F.Mode->LogoutCount, 0);
	F.First.Connection->CleanUp();
	TestEqual(TEXT("Normal cleanup reaches Logout once"), F.Mode->LogoutCount, 1);
	TestEqual(TEXT("Remaining participant preserved"), F.Mode->GetTestParticipantCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipEjectionCachedSweepTest,
	"ProjectRPG.Online.DungeonEjection.MembershipCachedSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonMembershipEjectionCachedSweepTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode)) return false;
	auto& Monitor = FRPGDungeonEjectionTestAccess::Monitor(*F.Mode);
	const FString Character = F.First.State->GetBackendCharacterId(), Steam = F.First.State->GetAuthenticatedSteamId();
	TestTrue(TEXT("Exact immutable evidence cached before admission"), F.RecordReceipt(Character, Steam));
	const FString OtherSession = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	F.First.State->SetAuthenticatedIdentity(Character, Steam, OtherSession);
	TestEqual(TEXT("Cached receipt cannot affect a different dungeon"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	F.First.State->SetAuthenticatedIdentity(Character, Steam, Session);
	uint64 Stale = 0; Monitor.BeginRead(Stale);
	Monitor.Configure(Session, Server);
	TestFalse(TEXT("Old read serial cannot sweep new scope"), FRPGDungeonEjectionTestAccess::Complete(*F.Mode, Stale, F.MakeReceipt(Character, Steam)));
	TestTrue(TEXT("New scope member remains authenticated"), F.First.State->HasAuthenticatedCharacter());
	TestTrue(TEXT("Restore exact evidence for current scope"), F.RecordReceipt(Character, Steam));
	uint64 FailedSerial = 0; Monitor.BeginRead(FailedSerial);
	TestFalse(TEXT("Later failure does not itself trigger completion sweep"), FRPGDungeonEjectionTestAccess::Complete(*F.Mode, FailedSerial, F.MakeReceipt(Character, Steam, false)));
	TestTrue(TEXT("Failed refresh preserves previously validated receipt"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	F.Mode->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("Non-authority sweep cannot act"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	F.Mode->SetRole(ROLE_Authority);
	TestEqual(TEXT("Next cached sweep handles late authenticated member"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 1);
	TestEqual(TEXT("Duplicate sweep cannot close twice"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	TestTrue(TEXT("Remaining member is not revoked"), F.Remaining.State->HasAuthenticatedCharacter());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonMembershipEjectionScopeReentryTest,
	"ProjectRPG.Online.DungeonEjection.MembershipScopeReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonMembershipEjectionScopeReentryTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Authoritative fixture mode"), F.Mode)) return false;
	TestTrue(TEXT("First exact receipt cached"), F.RecordReceipt(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId()));
	TestTrue(TEXT("Second exact receipt cached"), F.RecordReceipt(F.Remaining.State->GetBackendCharacterId(), F.Remaining.State->GetAuthenticatedSteamId()));
	F.First.State->OnCharacterIdentityChanged.AddDynamic(F.Mode, &ARPGDungeonEjectionTestGameMode::EndMembershipScopeOnIdentityChanged);
	TestEqual(TEXT("Scope invalidation during first notification stops remaining candidates"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 1);
	TestTrue(TEXT("First barrier remains raised"), F.First.State->IsAuthenticatedIdentityRevoked());
	TestTrue(TEXT("Remaining identity preserved after synchronous scope reset"), F.Remaining.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("Remaining transport not closed under ended scope"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	TestEqual(TEXT("Ended scope cannot act again"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDisconnectionExpiryEjectionTest,
	"ProjectRPG.Online.DungeonEjection.DisconnectionExpiry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDisconnectionExpiryEjectionTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F(true);
	if (!TestNotNull(TEXT("Expiry ejection mode"), F.Mode)) return false;
	auto& Monitor = FRPGDungeonEjectionTestAccess::Monitor(*F.Mode);
	const FString Character = F.First.State->GetBackendCharacterId(), Steam = F.First.State->GetAuthenticatedSteamId();
	TestTrue(TEXT("No immutable record grants no revocation reason"), FRPGDungeonEjectionTestAccess::Reason(*F.Mode, Session, Character, Steam).IsEmpty());
	uint64 Serial = 0;
	TestTrue(TEXT("Membership read starts"), Monitor.BeginRead(Serial));
	TestTrue(TEXT("Validated expiry completion applies the existing close boundary"),
		FRPGDungeonEjectionTestAccess::Complete(*F.Mode, Serial, F.MakeExpiry(Character, Steam)));
	TestEqual(TEXT("Expiry has a distinct admission/close reason"), FRPGDungeonEjectionTestAccess::Reason(*F.Mode, Session, Character, Steam),
		FString(TEXT("dungeon_member_disconnected_expired")));
	TestTrue(TEXT("Expiry evidence is cached"), Monitor.IsDisconnectedExpelled(Character, Steam));
	TestFalse(TEXT("Expiry never becomes an explicit abandonment receipt"), Monitor.IsExplicitlyAbandoned(Character, Steam));
	TestTrue(TEXT("Gameplay authority is revoked before cleanup"), F.First.State->IsAuthenticatedIdentityRevoked() && IsDungeonGameplayRevoked(F.First.Pawn));
	TestEqual(TEXT("Only expired connection closes"), F.First.Connection->GetConnectionState(), USOCK_Closed);
	TestEqual(TEXT("Remaining connection stays open"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	TestTrue(TEXT("Remaining identity and Pawn survive"), F.Remaining.State->HasAuthenticatedCharacter() && IsValid(F.Remaining.Pawn));
	TestEqual(TEXT("Close never manually logs out"), F.Mode->LogoutCount, 0);
	TestTrue(TEXT("Engine still owns deferred actor cleanup"), IsValid(F.First.Controller) && IsValid(F.First.Pawn));
	TestEqual(TEXT("Duplicate cache sweep cannot close twice"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	F.First.Connection->CleanUp();
	TestEqual(TEXT("Real engine cleanup logs out once"), F.Mode->LogoutCount, 1);
	TestEqual(TEXT("Only remaining participant survives"), F.Mode->GetTestParticipantCount(), 1);

	// A late 200/true confirmation must not resurrect this immutable expulsion.
	auto* Reconnecting = F.World->SpawnActor<ARPGPlayerController>();
	auto* NewState = F.World->SpawnActor<ARPGPlayerState>();
	Reconnecting->SetPlayerState(NewState); NewState->SetOwner(Reconnecting);
	auto* NewConnection = F.AddConnection(Reconnecting);
	Reconnecting->Player = NewConnection; Reconnecting->NetConnection = NewConnection;
	FRPGDungeonEjectionTestAccess::Confirm(*F.Mode, Reconnecting, NewConnection, Session, Character, Steam);
	TestFalse(TEXT("Late confirmation cannot publish identity"), NewState->HasAuthenticatedCharacter());
	TestFalse(TEXT("Late confirmation cannot activate PostLogin"), FRPGDungeonEjectionTestAccess::Activated(*F.Mode, Reconnecting));
	TestNull(TEXT("Late confirmation cannot spawn a Pawn"), Reconnecting->GetPawn());
	TestEqual(TEXT("Late connection is rejected without restoring membership"), NewConnection->GetConnectionState(), USOCK_Closed);
	TestTrue(TEXT("Different account cannot inherit the cached denial"),
		FRPGDungeonEjectionTestAccess::Reason(*F.Mode, Session, Character, F.Remaining.State->GetAuthenticatedSteamId()).IsEmpty());
	NewConnection->CleanUp();
	TestEqual(TEXT("Remaining connection still registered"), F.Driver->ClientConnections.Num(), 1);
	TestEqual(TEXT("Fixture never dispatches a socket packet"), F.First.Connection->SendCalls + NewConnection->SendCalls + F.Remaining.Connection->SendCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDisconnectionExpiryGuardsTest,
	"ProjectRPG.Online.DungeonEjection.DisconnectionExpiryGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonDisconnectionExpiryGuardsTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F;
	if (!TestNotNull(TEXT("Expiry guard mode"), F.Mode)) return false;
	auto& Monitor = FRPGDungeonEjectionTestAccess::Monitor(*F.Mode);
	const FString Character = F.First.State->GetBackendCharacterId(), Steam = F.First.State->GetAuthenticatedSteamId();
	auto Complete = [&](FRPGDungeonMembershipSnapshotResult Result)
	{
		uint64 Serial = 0;
		return Monitor.BeginRead(Serial) && FRPGDungeonEjectionTestAccess::Complete(*F.Mode, Serial, Result);
	};
	auto Failed = F.MakeExpiry(Character, Steam); Failed.bSuccess = false; Failed.HttpStatus = 500;
	TestFalse(TEXT("Failed reads never create expiry authority"), Complete(Failed));
	auto WrongServer = F.MakeExpiry(Character, Steam); WrongServer.Snapshot.ServerId = TEXT("other-server");
	TestFalse(TEXT("Wrong server scope cannot create expiry authority"), Complete(WrongServer));
	auto WrongSession = F.MakeExpiry(Character, Steam); WrongSession.Snapshot.DungeonSessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	TestFalse(TEXT("Wrong session scope cannot create expiry authority"), Complete(WrongSession));
	auto Invalid = F.MakeExpiry(Character, Steam); Invalid.Snapshot.DisconnectionExpulsions[0].ExpiredAt = TEXT("2026-10-17T00:09:59Z");
	TestFalse(TEXT("Invalid expiry receipt cannot create authority"), Complete(Invalid));
	auto Empty = F.MakeExpiry(Character, Steam); Empty.Snapshot.DisconnectionExpulsions.Reset();
	TestTrue(TEXT("Member absence alone never authorizes ejection"), Complete(Empty));
	TestTrue(TEXT("Foreign-account receipt is valid only for its own identity"), Complete(F.MakeExpiry(Character, TEXT("76561198000000003"))));
	TestTrue(TEXT("Wrong-account record leaves own admission open"), FRPGDungeonEjectionTestAccess::Reason(*F.Mode, Session, Character, Steam).IsEmpty());
	TestTrue(TEXT("All unproven reads retain authority"), F.First.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("No unproven read closes a socket"), F.First.Connection->GetConnectionState(), USOCK_Open);
	Monitor.Configure(Session, Server);
	const FString OtherSession = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
	F.First.State->SetAuthenticatedIdentity(Character, Steam, OtherSession);
	uint64 Serial = 0; Monitor.BeginRead(Serial);
	TestTrue(TEXT("Exact expiry receipt cached without immediate sweep"), Monitor.FinishRead(Serial, F.MakeExpiry(Character, Steam)));
	TestEqual(TEXT("A different live session is never revoked"), FRPGDungeonEjectionTestAccess::Sweep(*F.Mode), 0);
	TestTrue(TEXT("Admission reason is session scoped"), FRPGDungeonEjectionTestAccess::Reason(*F.Mode, OtherSession, Character, Steam).IsEmpty());
	TestFalse(TEXT("Failed refresh adds no new evidence"), Complete(Failed));
	TestTrue(TEXT("Empty refresh preserves previously confirmed expiry"), Complete(Empty));
	TestTrue(TEXT("Expiry cache is monotonic across errors/absence"), Monitor.IsDisconnectedExpelled(Character, Steam));
	F.First.State->SetAuthenticatedIdentity(Character, Steam, Session);
	auto* Child = NewObject<UChildConnection>(F.Driver); F.First.Connection->Children.Add(Child);
	TestFalse(TEXT("Closing a shared child connection cannot affect other players"), FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller));
	F.First.Connection->Children.Reset();
	TestTrue(TEXT("Rejected shared close keeps authority"), F.First.State->HasAuthenticatedCharacter());
	auto* Replacement = F.AddConnection(F.First.Controller);
	F.Mode->ReplaceConnectionController = F.First.Controller; F.Mode->ReplacementConnection = Replacement;
	F.First.State->OnCharacterIdentityChanged.AddDynamic(F.Mode, &ARPGDungeonEjectionTestGameMode::ReplaceConnectionOnIdentityChanged);
	TestFalse(TEXT("Reentrant replacement aborts expiry transport close"), FRPGDungeonEjectionTestAccess::Request(*F.Mode, F.First.Controller));
	TestTrue(TEXT("Expiry revocation is never rolled back after a replacement"), F.First.State->IsAuthenticatedIdentityRevoked());
	TestEqual(TEXT("Replacement is not closed by old expiry work"), Replacement->GetConnectionState(), USOCK_Open);
	TestEqual(TEXT("Old socket not closed under changed ownership"), F.First.Connection->GetConnectionState(), USOCK_Open);
	TestTrue(TEXT("Remaining player authority retained"), F.Remaining.State->HasAuthenticatedCharacter());
	return true;
}

struct FRPGDungeonConnectionTestAccess
{
	static UNetConnection* FindPreLogin(ARPGGameModeBase& Mode, const FString& Ticket)
	{
		return Mode.FindDungeonPreLoginConnection(Ticket, FUniqueNetIdRepl());
	}
	static auto TrackPreLogin(ARPGGameModeBase& Mode, UNetConnection* Connection, const FString& Ticket)
	{
		auto& Pending = Mode.PendingAdmissions.Add(Ticket);
		Pending.PreLoginConnection = MakeShared<ARPGGameModeBase::FPreLoginConnectionState>();
		auto& State = *Pending.PreLoginConnection;
		State.Ticket = Ticket; State.Connection = Connection; State.Driver = Connection->GetDriver();
		State.PlayerId = Connection->PlayerId;
		return Pending.PreLoginConnection;
	}
	template <typename TState> static bool PreLoginCurrent(ARPGGameModeBase& Mode, const TState& State)
	{
		return Mode.IsDungeonPreLoginConnectionCurrent(*State);
	}
	static void PollPreLogin(ARPGGameModeBase& Mode) { Mode.PollDungeonPreLoginConnections(); }
	static void SetPreLoginCompletion(ARPGGameModeBase& Mode, const FString& Ticket, AGameModeBase::FOnPreLoginCompleteDelegate Completion)
	{ Mode.PendingAdmissions.FindChecked(Ticket).Completion = MoveTemp(Completion); }
	static void ExpirePreLogin(ARPGGameModeBase& Mode, const FString& Ticket) { Mode.RetainExpiredDungeonPreLogin(Ticket); }
	static bool ExpiredPreLoginRetained(ARPGGameModeBase& Mode, const FString& Ticket)
	{
		const auto* Pending = Mode.PendingAdmissions.Find(Ticket);
		return Pending && Pending->bConsumeRecoveryExpired && !Pending->Completion.IsBound();
	}
	static void StoreConsumedPreLogin(ARPGGameModeBase& Mode, const FString& Ticket, int64 Generation)
	{
		ARPGGameModeBase::FPendingAdmission Pending;
		check(Mode.PendingAdmissions.RemoveAndCopyValue(Ticket, Pending));
		// Inject only the known backend receipt, not a successful HTTP/durable report.
		auto& Admission = Mode.ValidatedAdmissions.Add(Ticket);
		Admission = {RPGDungeonEjectionTests::Session, TEXT("30405060-7080-90a0-b0c0-d0e0f0010203"), TEXT("76561198000000003"), Generation};
		Admission.PreLoginConnection = Pending.PreLoginConnection;
	}
	static int64 RetainedPreLoginGeneration(ARPGGameModeBase& Mode, const FString& Ticket)
	{
		const auto* Admission = Mode.ValidatedAdmissions.Find(Ticket);
		return Admission ? Admission->ConnectionGeneration : 0;
	}
	static TWeakObjectPtr<UNetConnection>& CleanupIdentity(APlayerController* Controller)
	{
		return CastChecked<ARPGPlayerController>(Controller)->ConnectionBeingCleanedUp;
	}
	static auto Pending(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection)
	{
		auto& Awaiting = Mode.AdmissionsAwaitingBackend.Add(Controller);
		Awaiting.PlayerState = Controller->GetPlayerState<ARPGPlayerState>();
		Awaiting.Connection = Connection;
		Awaiting.RequestState = MakeShared<ARPGGameModeBase::FAdmissionRequestState>();
		return Awaiting.RequestState;
	}
	static void Activate(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection)
	{
		auto* State = Controller->GetPlayerState<ARPGPlayerState>();
		State->SetAuthenticatedIdentity(State->GetBackendCharacterId(), State->GetAuthenticatedSteamId(),
			State->GetBackendDungeonSessionId(), 42);
		auto& Active = Mode.ActivatedAdmissions.Add(Controller);
		Active.PlayerState = State; Active.Connection = Connection;
		Active.Admission = {State->GetBackendDungeonSessionId(), State->GetBackendCharacterId(),
			State->GetAuthenticatedSteamId(), State->GetBackendConnectionGeneration()};
	}
	static bool Unexpected(ARPGGameModeBase& Mode, APlayerController* Controller)
	{
		ARPGGameModeBase::FValidatedAdmission Expected;
		return Mode.GetUnexpectedDungeonConnectionCleanup(Controller, Expected);
	}
	static bool OwnedCleanup(ARPGGameModeBase& Mode, APlayerController* Controller, UNetConnection* Connection)
	{
		return Mode.IsOwnedDungeonConnectionCleanup(Controller, Connection, Controller->GetPlayerState<ARPGPlayerState>());
	}
	static bool HasActivation(ARPGGameModeBase& Mode, APlayerController* Controller)
	{
		return Mode.ActivatedAdmissions.Contains(TWeakObjectPtr<APlayerController>(Controller));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPreLoginConnectionRecoveryTest,
	"ProjectRPG.Online.DungeonPreLoginConnectionRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPreLoginConnectionRecoveryTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	using Access = FRPGDungeonConnectionTestAccess;
	FFixture F(true);
	if (!TestNotNull(TEXT("Authoritative prelogin fixture"), F.Mode)) return false;
	const FString Ticket = TEXT("PreLogin-Ticket-A");
	auto* Original = F.AddConnection(nullptr);
	Original->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=") + Ticket;
	Original->SetClientLoginState(EClientLoginState::LoggingIn);
	TestTrue(TEXT("Unique ticket resolves the exact parent before any player exists"), Access::FindPreLogin(*F.Mode, Ticket) == Original);
	TestNull(TEXT("Ticket comparison is case-sensitive"), Access::FindPreLogin(*F.Mode, Ticket.ToLower()));
	Original->OwningActor = F.First.Controller;
	TestNull(TEXT("An already owned connection is not a new login"), Access::FindPreLogin(*F.Mode, Ticket));
	Original->OwningActor = nullptr;
	auto* Replacement = F.AddConnection(nullptr);
	Replacement->RequestURL = Original->RequestURL;
	Replacement->SetClientLoginState(EClientLoginState::LoggingIn);
	TestNull(TEXT("Ambiguous replay does not choose either socket"), Access::FindPreLogin(*F.Mode, Ticket));
	Replacement->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=AnotherTicket");
	Original->Children.Add(NewObject<UChildConnection>(Original));
	TestNull(TEXT("A shared parent cannot be tracked as an individual login"), Access::FindPreLogin(*F.Mode, Ticket));
	Original->Children.Reset();
	auto Tracked = Access::TrackPreLogin(*F.Mode, Original, Ticket);
	Access::PollPreLogin(*F.Mode);
	TestTrue(TEXT("Live pending original remains usable"), Access::PreLoginCurrent(*F.Mode, Tracked));
	TestEqual(TEXT("Loading without socket loss does not start a timer"), Tracked->LostAtUtc.GetTicks(), int64(0));
	Original->SetClientLoginState(EClientLoginState::Welcomed);
	TestTrue(TEXT("Welcome-to-Join loading retains original ownership"), Access::PreLoginCurrent(*F.Mode, Tracked));
	Access::PollPreLogin(*F.Mode);
	TestEqual(TEXT("Welcomed is not a disconnect"), Tracked->LostAtUtc.GetTicks(), int64(0));
	F.Driver->ClientConnections.Remove(Original);
	Access::PollPreLogin(*F.Mode);
	TestFalse(TEXT("Missing list entry cannot complete engine prelogin"), Access::PreLoginCurrent(*F.Mode, Tracked));
	TestEqual(TEXT("List absence alone cannot fabricate a disconnect"), Tracked->LostAtUtc.GetTicks(), int64(0));
	F.Driver->ClientConnections.Add(Original);
	Original->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=ChangedTicket");
	Access::PollPreLogin(*F.Mode);
	TestFalse(TEXT("Changed ticket cannot receive the old completion"), Access::PreLoginCurrent(*F.Mode, Tracked));
	TestEqual(TEXT("Changed identity alone cannot fabricate a disconnect"), Tracked->LostAtUtc.GetTicks(), int64(0));
	Original->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=") + Ticket;
	{
		TGuardValue<FDateTime> Loss(Tracked->LostAtUtc, FDateTime::UtcNow());
		TestFalse(TEXT("An observed loss is irreversible even if an object appears open"), Access::PreLoginCurrent(*F.Mode, Tracked));
	}
	const FDateTime BeforeClose = FDateTime::UtcNow();
	Original->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, TEXT("native-prelogin-loss")));
	// A whole-world/driver transition is never attributed to one member.
	F.World->bIsTearingDown = true;
	Access::PollPreLogin(*F.Mode);
	TestEqual(TEXT("Teardown does not create a player loss"), Tracked->LostAtUtc.GetTicks(), int64(0));
	F.World->bIsTearingDown = false;
	auto* OtherDriver = NewObject<UIpNetDriver>(); OtherDriver->SetWorld(F.World);
	F.World->SetNetDriver(OtherDriver);
	Access::PollPreLogin(*F.Mode);
	TestEqual(TEXT("Driver replacement does not create a player loss"), Tracked->LostAtUtc.GetTicks(), int64(0));
	F.World->SetNetDriver(F.Driver); OtherDriver->SetWorld(nullptr);
	Access::PollPreLogin(*F.Mode);
	const FDateTime FirstLoss = Tracked->LostAtUtc;
	TestTrue(TEXT("Loss captured while ticket response is still pending"), FirstLoss >= BeforeClose && FirstLoss <= FDateTime::UtcNow());
	TestFalse(TEXT("Closed original cannot receive an engine completion"), Access::PreLoginCurrent(*F.Mode, Tracked));
	Original->CleanUp();
	Replacement->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=") + Ticket;
	TestTrue(TEXT("Only a new admission may select the replacement"), Access::FindPreLogin(*F.Mode, Ticket) == Replacement);
	TestFalse(TEXT("Old tracked admission never rematches a replacement socket"), Access::PreLoginCurrent(*F.Mode, Tracked));
	TestEqual(TEXT("Pre-controller cleanup does not fabricate a Logout"), F.Mode->LogoutCount, 0);
	constexpr int64 ConsumedGeneration = 9007199254740993LL;
	Access::StoreConsumedPreLogin(*F.Mode, Ticket, ConsumedGeneration);
	Access::PollPreLogin(*F.Mode);
	TestEqual(TEXT("Known consumed generation retained exactly when transport is unavailable"), Access::RetainedPreLoginGeneration(*F.Mode, Ticket), ConsumedGeneration);
	TestFalse(TEXT("Unavailable transport cannot claim durable enqueue"), Tracked->bLossDurablyQueued);
	TestTrue(TEXT("Failed report is retained with bounded retry"), Tracked->NextReportAttemptAt > 0.0);
	Access::PollPreLogin(*F.Mode);
	TestEqual(TEXT("Delayed receipt and repeated polls preserve first loss UTC"), Tracked->LostAtUtc, FirstLoss);
	TestTrue(TEXT("Remaining party authority and pawn survive"), F.Remaining.State->HasAuthenticatedCharacter() && IsValid(F.Remaining.Pawn));
	TestEqual(TEXT("Remaining socket stays open"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	TestEqual(TEXT("Replacement socket is not closed by original loss"), Replacement->GetConnectionState(), USOCK_Open);
	const FString ExpiredTicket = TEXT("Expired-PreLogin-Ticket");
	auto* ExpiredConnection = F.AddConnection(nullptr);
	ExpiredConnection->RequestURL = TEXT("/Game/Maps/Test?JoinTicket=") + ExpiredTicket;
	ExpiredConnection->SetClientLoginState(EClientLoginState::LoggingIn);
	auto ExpiredTracker = Access::TrackPreLogin(*F.Mode, ExpiredConnection, ExpiredTicket);
	int32 ExpiredCompletions = 0;
	Access::SetPreLoginCompletion(*F.Mode, ExpiredTicket, AGameModeBase::FOnPreLoginCompleteDelegate::CreateLambda(
		[&ExpiredCompletions](const FString&) { ++ExpiredCompletions; }));
	Access::ExpirePreLogin(*F.Mode, ExpiredTicket); Access::ExpirePreLogin(*F.Mode, ExpiredTicket);
	TestEqual(TEXT("Expired live PreLogin receives exactly one terminal engine failure"), ExpiredCompletions, 1);
	TestTrue(TEXT("Expired original tracker survives without retaining an engine delegate"), Access::ExpiredPreLoginRetained(*F.Mode, ExpiredTicket));
	TestEqual(TEXT("A deadline alone cannot fabricate socket loss"), ExpiredTracker->LostAtUtc.GetTicks(), int64(0));
	ExpiredConnection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, TEXT("native-expired-prelogin-loss")));
	Access::PollPreLogin(*F.Mode);
	const FDateTime ExpiredLoss = ExpiredTracker->LostAtUtc;
	TestTrue(TEXT("Actual loss after terminal failure is still observed"), ExpiredLoss.GetTicks() > 0);
	Access::PollPreLogin(*F.Mode);
	TestTrue(TEXT("Missing backend transport retains the original expired tracker and first UTC"),
		Access::ExpiredPreLoginRetained(*F.Mode, ExpiredTicket) && ExpiredTracker->LostAtUtc == ExpiredLoss);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonConnectionCleanupTest,
	"ProjectRPG.Online.DungeonConnectionCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonConnectionCleanupTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F(true);
	if (!TestNotNull(TEXT("Authoritative connection fixture"), F.Mode)) return false;
	FRPGDungeonConnectionTestAccess::Activate(*F.Mode, F.First.Controller, F.First.Connection);
	TestFalse(TEXT("Ordinary Logout is not proof of a socket cleanup"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	TGuardValue<TWeakObjectPtr<UNetConnection>> CleanupScope(
		FRPGDungeonConnectionTestAccess::CleanupIdentity(F.First.Controller), TWeakObjectPtr<UNetConnection>(F.First.Connection));
	TestTrue(TEXT("Exact active generation is classified unexpected"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	// Reproduce the engine state inside Logout, after OnNetCleanup clears Player.
	F.First.Connection->PlayerController = nullptr;
	F.First.Controller->Player = nullptr; F.First.Controller->NetConnection = nullptr;
	TestTrue(TEXT("Engine-cleared pointers retain exact cleanup ownership"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	TestTrue(TEXT("Pending admission uses the same cleared-pointer-safe guard"), FRPGDungeonConnectionTestAccess::OwnedCleanup(*F.Mode, F.First.Controller, F.First.Connection));
	TestFalse(TEXT("Another member cannot inherit the cleanup sentinel"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.Remaining.Controller));
	F.First.Connection->PlayerController = F.Remaining.Controller;
	TestFalse(TEXT("Replacement controller cannot be reported"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	F.First.Connection->PlayerController = nullptr;
	F.First.Controller->Player = F.Remaining.Connection;
	TestFalse(TEXT("Replacement socket cannot be reported"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	F.First.Controller->Player = nullptr;
	F.First.State->SetAuthenticatedIdentity(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId(), Session, 43);
	TestFalse(TEXT("Stale activation cannot report a new generation"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	F.First.State->SetAuthenticatedIdentity(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId(), Session, 42);
	F.World->bIsTearingDown = true;
	TestFalse(TEXT("World shutdown is not a member disconnect"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	F.World->bIsTearingDown = false;
	TestTrue(TEXT("Explicit receipt recorded"), F.RecordReceipt(F.First.State->GetBackendCharacterId(), F.First.State->GetAuthenticatedSteamId()));
	TestFalse(TEXT("Explicit abandonment is not unexpected loss"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	F.First.Controller->Player = F.First.Connection; F.First.Controller->NetConnection = F.First.Connection;
	F.First.Connection->PlayerController = F.First.Controller;
	FRPGDungeonConnectionTestAccess::CleanupIdentity(F.First.Controller).Reset();
	{
		FFixture PendingFixture(true);
		if (!TestNotNull(TEXT("Pending cleanup fixture"), PendingFixture.Mode)) return false;
		auto Request = FRPGDungeonConnectionTestAccess::Pending(*PendingFixture.Mode,
			PendingFixture.First.Controller, PendingFixture.First.Connection);
		const FDateTime BeforeCleanup = FDateTime::UtcNow();
		PendingFixture.First.Connection->CleanUp();
		TestTrue(TEXT("Real OnNetCleanup marks the pending response despite cleared pointers"), Request->bConnectionLostBeforeResponse);
		TestTrue(TEXT("Observation is captured at cleanup, before the delayed HTTP response"),
			Request->ConnectionLostAtUtc >= BeforeCleanup && Request->ConnectionLostAtUtc <= FDateTime::UtcNow());
		TestFalse(TEXT("A fixture without a backend transport cannot claim durable enqueue"), Request->bConnectionLossDurablyQueued);
		TestEqual(TEXT("Pending cleanup uses normal Logout exactly once"), PendingFixture.Mode->LogoutCount, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlannedDisconnectTest,
	"ProjectRPG.Online.DungeonPlannedDisconnect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlannedDisconnectTest::RunTest(const FString&)
{
	using namespace RPGDungeonEjectionTests;
	FFixture F(true);
	if (!TestNotNull(TEXT("Authoritative planned-disconnect fixture"), F.Mode)) return false;
	UFunction* Request = ARPGPlayerController::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ARPGPlayerController, ServerRequestDungeonLobbyDisconnect));
	if (!TestNotNull(TEXT("Owning actor server RPC is reflected"), Request)) return false;
	TestTrue(TEXT("Intent uses reliable server RPC, not a multicast/client identity write"),
		Request->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
	TestEqual(TEXT("Client cannot supply identity, connection or generation"), static_cast<int32>(Request->NumParms), 0);
	TestFalse(TEXT("Unconfirmed player cannot suppress a disconnect"), F.Mode->RequestPlannedDungeonLobbyDisconnect(F.First.Controller));
	FRPGDungeonConnectionTestAccess::Activate(*F.Mode, F.First.Controller, F.First.Connection);
	F.First.Controller->Player = F.Remaining.Connection;
	TestFalse(TEXT("Mismatched socket cannot be marked planned"), F.Mode->RequestPlannedDungeonLobbyDisconnect(F.First.Controller));
	F.First.Controller->Player = F.First.Connection;
	F.Mode->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client role cannot classify a server exit"), F.Mode->RequestPlannedDungeonLobbyDisconnect(F.First.Controller));
	F.Mode->SetRole(ROLE_Authority);
	const FString Character = F.First.State->GetBackendCharacterId();
	TestTrue(TEXT("Planned signal synchronously closes its exact socket"), F.Mode->RequestPlannedDungeonLobbyDisconnect(F.First.Controller));
	TestEqual(TEXT("No flag-only exemption while still connected"), F.First.Connection->GetConnectionState(), USOCK_Closed);
	TestFalse(TEXT("Duplicate cannot perform another close"), F.Mode->RequestPlannedDungeonLobbyDisconnect(F.First.Controller));
	TestTrue(TEXT("Emergency return is not identity revocation"), F.First.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("Persistent identity retained before engine cleanup"), F.First.State->GetBackendCharacterId(), Character);
	{
		TGuardValue<TWeakObjectPtr<UNetConnection>> CleanupScope(
			FRPGDungeonConnectionTestAccess::CleanupIdentity(F.First.Controller), TWeakObjectPtr<UNetConnection>(F.First.Connection));
		TestFalse(TEXT("Planned cleanup cannot start the grace timer"), FRPGDungeonConnectionTestAccess::Unexpected(*F.Mode, F.First.Controller));
	}
	F.First.Connection->CleanUp();
	TestEqual(TEXT("Real engine cleanup logs out once"), F.Mode->LogoutCount, 1);
	TestFalse(TEXT("Activation removed by real Logout"), FRPGDungeonConnectionTestAccess::HasActivation(*F.Mode, F.First.Controller));
	TestEqual(TEXT("Remaining socket untouched"), F.Remaining.Connection->GetConnectionState(), USOCK_Open);
	TestTrue(TEXT("Remaining combat member retained"), IsValid(F.Remaining.Pawn) && F.Remaining.State->HasAuthenticatedCharacter());
	TestEqual(TEXT("Native fixture never sent a packet"), F.First.Connection->SendCalls + F.Remaining.Connection->SendCalls, 0);
	return true;
}

#endif
