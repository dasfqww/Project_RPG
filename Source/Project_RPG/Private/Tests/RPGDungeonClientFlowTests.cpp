#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "IpConnection.h"
#include "IpNetDriver.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

// Inject HTTP responses and failure callbacks. Production travel/identity guards
// run unchanged; no backend requests, sockets, Steam writes or map loads occur.
struct FRPGDungeonFlowTestAccess
{
	static void Seed(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonSession& Session)
	{
		Flow->CancelLocalRecovery();
		Flow->CurrentDungeonSession = Session;
		Flow->SelectedCharacterId = TEXT("own-character");
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::None;
		Flow->FlowState = ERPGDungeonClientFlowState::InSession;
		Flow->LastErrorCode.Reset();
		Flow->ClearRejectedDungeonAdmission();
		Flow->LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");
	}
	static void BeginReturn(URPGDungeonSessionSubsystem* Flow, APlayerController* Controller)
	{
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::Return;
		Flow->PendingPlayerController = Controller;
		Flow->PendingReturnSessionId = Flow->CurrentDungeonSession.DungeonSessionId;
		Flow->FlowState = ERPGDungeonClientFlowState::RefreshingSession;
	}
	static void Receive(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonSession& Session, bool bSuccess = true)
	{
		Flow->HandleDungeonSessionUpdated(Session, bSuccess);
	}
	static void CompleteMap(URPGDungeonSessionSubsystem* Flow, UWorld* World)
	{
		Flow->HandleLobbyMapLoaded(World);
	}
	static void BeginMapCompletion(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->FlowState = ERPGDungeonClientFlowState::ReturningToLobby;
	}
	static void SimulateDestinationLoaded(URPGDungeonSessionSubsystem* Flow, UWorld* World)
	{
		// Simulate the post-load identity match without loading/mutating a real map.
		Flow->LobbyMapPackage = World->GetOutermost()->GetName();
		Flow->HandleLobbyMapLoaded(World);
	}
	static void TravelFailure(URPGDungeonSessionSubsystem* Flow, UWorld* World)
	{
		Flow->HandleLobbyTravelFailure(World, ETravelFailure::PackageMissing, TEXT("ignored test URL"));
	}
	static void SetMap(URPGDungeonSessionSubsystem* Flow, const FString& Package)
	{
		Flow->LobbyMapPackage = Package;
	}
	static bool RunRecovery(URPGDungeonSessionSubsystem* Flow)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Flow->LocalRecoveryTicker);
		return Flow->TickLocalRecovery(0.f);
	}
	static void CancelRecovery(URPGDungeonSessionSubsystem* Flow) { Flow->CancelLocalRecovery(); }
	static void WaitForClose(URPGDungeonSessionSubsystem* Flow, UNetConnection* Connection, bool bExpired = false)
	{
		Flow->PlannedDisconnectConnection = Connection;
		Flow->PlannedDisconnectDeadline = FPlatformTime::Seconds() + (bExpired ? -1.0 : 60.0);
	}
	static void BeginConnecting(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->FlowState = ERPGDungeonClientFlowState::Connecting;
	}
	static void BeginTicket(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::Connect;
		Flow->FlowState = ERPGDungeonClientFlowState::RequestingJoinTicket;
	}
	static void PendingTarget(URPGDungeonSessionSubsystem* Flow, APlayerController* Controller, const FString& Address)
	{
		BeginTicket(Flow);
		Flow->PendingPlayerController = Controller;
		Flow->PendingServerAddress = Address;
	}
	static void IssueTicket(URPGDungeonSessionSubsystem* Flow, const FString& SessionId, const FString& CharacterId)
	{
		Flow->HandleJoinTicketIssued(SessionId, CharacterId, TEXT("bound-target-fixture-ticket"),
			TEXT("2099-01-01T00:00:00Z"), true, FString());
	}
	static void RejectTicket(URPGDungeonSessionSubsystem* Flow, const FString& SessionId,
		const FString& CharacterId, const FString& ErrorCode)
	{
		Flow->HandleJoinTicketIssued(SessionId, CharacterId, FString(), FString(), false, ErrorCode);
	}
	static void BeginMembershipRecheck(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::RecheckMembership;
		Flow->FlowState = ERPGDungeonClientFlowState::RefreshingSession;
	}
	static void BeginDepartureRecovery(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonSession& Session, bool bCancel)
	{
		Seed(Flow, Session);
		Flow->PendingDepartureSession = Session.DungeonSessionId;
		Flow->PendingDepartureCharacter = TEXT("own-character");
		Flow->PendingDepartureSteam = TEXT("own-steam");
		Flow->PendingDepartureRequest = Session.DepartureRequestId;
		Flow->PendingDepartureRevision = Session.DepartureRequestRevision;
		Flow->bPendingDepartureCancel = bCancel;
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::Refresh;
	}
	static bool HasPendingDeparture(URPGDungeonSessionSubsystem* Flow) { return !Flow->PendingDepartureRequest.IsEmpty(); }
	static void NetworkFailure(URPGDungeonSessionSubsystem* Flow, UWorld* World, UNetDriver* Driver)
	{
		Flow->HandleDungeonNetworkFailure(World, Driver, ENetworkFailure::PendingConnectionFailure,
			TEXT("ignored server URL?JoinTicket=never-forward-this"));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonClientReturnTest, "ProjectRPG.Online.DungeonReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonClientReturnTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Return test world"), World)) { return false; }
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>();
	World->SetGameInstance(Instance);
	ON_SCOPE_EXIT
	{
		Context.TravelURL.Reset(); // Never execute the scheduled map change.
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController(); // No real LocalPlayer/viewport in this isolated world.
	if (!TestTrue(TEXT("Standalone test controller is local"), Controller->IsLocalController())) { return false; }
	URPGDungeonSessionSubsystem* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("own-session");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);

	for (const TCHAR* State : { TEXT("Waiting"), TEXT("Loading"), TEXT("InProgress"), TEXT("SettlementPending") })
	{
		Session.State = State;
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		TestTrue(TEXT("Unfinished sessions retain the character lease"), Flow->HasActiveDungeonSession());
		if (Session.State == TEXT("SettlementPending"))
		{
			TestFalse(TEXT("Cannot select another character during settlement"), Flow->SelectCharacter(TEXT("other-character")));
			TestFalse(TEXT("Cannot create another dungeon during settlement"), Flow->CreateDungeonSession(TEXT("Dungeon.Next"), TEXT("Hard")));
		}
		FRPGDungeonFlowTestAccess::BeginReturn(Flow, Controller);
		FRPGDungeonFlowTestAccess::Receive(Flow, Session);
		TestEqual(TEXT("Unfinished backend state rejects return"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_not_finished")));
		TestTrue(TEXT("Return rejection cannot schedule local travel"), Context.TravelURL.IsEmpty());
		TestTrue(TEXT("Rejected return keeps the active session"), Flow->HasActiveDungeonSession());
		TestEqual(TEXT("Selected character survives return rejection"), Flow->GetSelectedCharacterId(), Member.CharacterId);
	}

	Session.State = TEXT("Cleared");
	for (const TCHAR* Package : { TEXT("127.0.0.1:7777"), TEXT("/Game/Maps/LobbyMap?JoinTicket=invalid"),
		TEXT("/Game/Maps/NonexistentDungeonReturnFixture") })
	{
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		FRPGDungeonFlowTestAccess::SetMap(Flow, Package);
		TestFalse(TEXT("Remote/options/missing destination is rejected before HTTP"), Flow->ReturnToLobby(Controller));
		TestEqual(TEXT("Invalid map has a stable UI error"), Flow->GetLastErrorCode(), FString(TEXT("lobby_map_unavailable")));
		TestTrue(TEXT("Invalid map cannot schedule travel"), Context.TravelURL.IsEmpty());
	}
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestFalse(TEXT("Missing local controller rejects the public return entry"), Flow->ReturnToLobby(nullptr));
	TestEqual(TEXT("Missing local controller has a stable UI error"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_return_not_available")));
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginReturn(Flow, Controller);
	FRPGDungeonSession Unrelated = Session;
	Unrelated.DungeonSessionId = TEXT("another-session");
	FRPGDungeonFlowTestAccess::Receive(Flow, Unrelated);
	TestTrue(TEXT("Unrelated HTTP callback leaves the request pending"), Flow->IsRequestInFlight());
	TestTrue(TEXT("Unrelated session cannot schedule travel"), Context.TravelURL.IsEmpty());
	FRPGDungeonFlowTestAccess::Receive(Flow, Session, false);
	TestEqual(TEXT("HTTP failure does not authorize return"), Flow->GetLastErrorCode(), FString(TEXT("refresh_dungeon_session_failed")));
	TestTrue(TEXT("HTTP failure leaves travel empty"), Context.TravelURL.IsEmpty());

	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginReturn(Flow, Controller);
	FRPGDungeonSession WrongMember = Session;
	WrongMember.Members[0].CharacterId = TEXT("other-character");
	FRPGDungeonFlowTestAccess::Receive(Flow, WrongMember);
	TestEqual(TEXT("Terminal response must contain our character"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_membership_mismatch")));
	TestTrue(TEXT("Wrong membership cannot schedule travel"), Context.TravelURL.IsEmpty());
	const FRPGDungeonSession Retained = Flow->GetCurrentDungeonSession();
	if (TestEqual(TEXT("Wrong membership preserves trusted member count"), Retained.Members.Num(), 1))
	{
		TestEqual(TEXT("Wrong membership preserves last trusted session"), Retained.Members[0].CharacterId, Member.CharacterId);
	}

	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginReturn(Flow, nullptr);
	FRPGDungeonFlowTestAccess::Receive(Flow, Session);
	TestEqual(TEXT("Controller must still exist when HTTP completes"), Flow->GetLastErrorCode(), FString(TEXT("return_controller_unavailable")));
	TestTrue(TEXT("Missing controller cannot schedule travel"), Context.TravelURL.IsEmpty());

	for (const TCHAR* State : { TEXT("Cleared"), TEXT("Failed") })
	{
		Session.State = State;
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		FRPGDungeonFlowTestAccess::BeginReturn(Flow, Controller);
		FRPGDungeonFlowTestAccess::Receive(Flow, Session);
		TestEqual(TEXT("Terminal backend state starts local return"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
		TestTrue(TEXT("Return waits for a core tick before local travel"), Context.TravelURL.IsEmpty());
		FRPGDungeonFlowTestAccess::RunRecovery(Flow);
		TestEqual(TEXT("Return uses the configured local package only"), Context.TravelURL, FString(TEXT("/Game/Maps/LobbyMap")));
		TestEqual(TEXT("Absolute travel discards server URL and join options"), Context.TravelType, TRAVEL_Absolute);
		TestFalse(TEXT("Terminal session no longer owns an active lease"), Flow->HasActiveDungeonSession());
		TestFalse(TEXT("Double-click cannot start another return"), Flow->ReturnToLobby(Controller));
		Flow->ResetDungeonFlow(false);
		TestEqual(TEXT("Reset cannot discard selection while travel is in flight"), Flow->GetSelectedCharacterId(), Member.CharacterId);
		FRPGDungeonFlowTestAccess::CompleteMap(Flow, World);
		TestTrue(TEXT("Unrelated post-load map cannot unlock the return"), Flow->IsRequestInFlight());
		if (Session.State == TEXT("Cleared"))
		{
			FRPGDungeonFlowTestAccess::SimulateDestinationLoaded(Flow, World);
			TestEqual(TEXT("Matching destination returns UI flow to idle"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Idle);
		}
		else
		{
			FRPGDungeonFlowTestAccess::TravelFailure(Flow, World);
			TestEqual(TEXT("Travel failure releases input guard"), Flow->GetLastErrorCode(), FString(TEXT("lobby_travel_failed")));
		}
		TestFalse(TEXT("Completion/failure releases travel guard"), Flow->IsRequestInFlight());
		TestEqual(TEXT("Selected character survives return"), Flow->GetSelectedCharacterId(), Member.CharacterId);
		Context.TravelURL.Reset();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPIELobbyCompletionTest, "ProjectRPG.Online.DungeonPIELobbyCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPIELobbyCompletionTest::RunTest(const FString&)
{
	// Simulate post-load identity only: no real map load, auth, HTTP or travel.
	const FString Name = TEXT("RPGReturnFixture_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Destination = TEXT("/Game/Maps/") + Name;
	UPackage* Package = CreatePackage(*(TEXT("/Game/Maps/UEDPIE_3_") + Name));
	UWorld* Loaded = UWorld::CreateWorld(EWorldType::PIE, false, FName(*Name), Package);
	if (!TestNotNull(TEXT("Isolated PIE destination"), Loaded)) return false;
	ON_SCOPE_EXIT { Loaded->DestroyWorld(false); };
	UGameInstance* Instance = NewObject<UGameInstance>();
	UGameInstance* OtherInstance = NewObject<UGameInstance>();
	Loaded->SetGameInstance(Instance);
	URPGDungeonSessionSubsystem* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("completed-session");
	Session.State = TEXT("Cleared");
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::SetMap(Flow, Destination);
	FRPGDungeonFlowTestAccess::BeginMapCompletion(Flow);
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, nullptr);
	TestEqual(TEXT("Missing world retains return guard"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	Loaded->SetGameInstance(OtherInstance);
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, Loaded);
	TestEqual(TEXT("Another instance cannot complete return"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	Loaded->SetGameInstance(Instance);
	FRPGDungeonFlowTestAccess::SetMap(Flow, Destination + TEXT("_Other"));
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, Loaded);
	TestEqual(TEXT("Another PIE map cannot complete return"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	FRPGDungeonFlowTestAccess::SetMap(Flow, Destination.ToLower());
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, Loaded);
	TestEqual(TEXT("Existing FString package casing comparison is preserved"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Idle);
	FRPGDungeonFlowTestAccess::BeginMapCompletion(Flow);
	FRPGDungeonFlowTestAccess::SetMap(Flow, Destination);
	Loaded->WorldType = EWorldType::Game;
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, Loaded);
	TestEqual(TEXT("Game worlds cannot borrow PIE prefix normalization"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	Loaded->WorldType = EWorldType::PIE;
	FRPGDungeonFlowTestAccess::CompleteMap(Flow, Loaded);
	TestEqual(TEXT("Matching PIE destination completes return"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Idle);
	TestFalse(TEXT("Matching destination releases travel input guard"), Flow->IsRequestInFlight());
	TestEqual(TEXT("Selected character remains intact"), Flow->GetSelectedCharacterId(), FString(TEXT("own-character")));
	TestEqual(TEXT("Completed session remains intact"), Flow->GetCurrentDungeonSession().DungeonSessionId, Session.DungeonSessionId);
	TestEqual(TEXT("Terminal backend state remains intact"), Flow->GetCurrentDungeonSession().State, Session.State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonClientRecoveryTest, "ProjectRPG.Online.DungeonRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonClientRecoveryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Recovery world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>();
	World->SetGameInstance(Instance);
	URPGDungeonSessionSubsystem* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	ON_SCOPE_EXIT
	{
		FRPGDungeonFlowTestAccess::CancelRecovery(Flow);
		Context.TravelURL.Reset();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("own-session");
	Session.ServerId = TEXT("own-server");
	Session.ServerAddress = TEXT("127.0.0.1:7777");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);
	for (const TCHAR* State : {TEXT("InProgress"), TEXT("SettlementPending"), TEXT("Cleared"), TEXT("Failed")})
	{
		Session.State = State;
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		// A timed-out read need not finish before a local-only disconnect can escape.
		FRPGDungeonFlowTestAccess::BeginReturn(Flow, Controller);
		TestTrue(TEXT("Recovery does not require HTTP or a finished backend session"), Flow->DisconnectToLobby(Controller));
		TestTrue(TEXT("Queued recovery holds the input guard"), Flow->IsRequestInFlight());
		TestFalse(TEXT("Recovery rejects a double click"), Flow->DisconnectToLobby(Controller));
		FRPGDungeonFlowTestAccess::Receive(Flow, FRPGDungeonSession(), false);
		TestEqual(TEXT("Cancelled read callback cannot change the recovery state"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
		FRPGDungeonFlowTestAccess::TravelFailure(Flow, World);
		TestEqual(TEXT("Original connection diagnostics cannot cancel queued recovery"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
		TestFalse(TEXT("Recovery core ticker is one-shot"), FRPGDungeonFlowTestAccess::RunRecovery(Flow));
		TestEqual(TEXT("Recovery schedules only the validated lobby package"), Context.TravelURL, FString(TEXT("/Game/Maps/LobbyMap")));
		TestEqual(TEXT("Recovery travel discards all old URL options"), Context.TravelType, TRAVEL_Absolute);
		TestEqual(TEXT("Local travel cannot change backend session state"), Flow->GetCurrentDungeonSession().State, FString(State));
		TestEqual(TEXT("Local travel cannot clear backend membership"), Flow->GetCurrentDungeonSession().Members.Num(), 1);
		TestEqual(TEXT("Selected character survives escape"), Flow->GetSelectedCharacterId(), Member.CharacterId);
		TestEqual(TEXT("Recovery cannot retarget another server"), Flow->GetCurrentDungeonSession().ServerId, Session.ServerId);
		FRPGDungeonFlowTestAccess::CompleteMap(Flow, World);
		TestTrue(TEXT("Wrong post-load map cannot unlock recovery"), Flow->IsRequestInFlight());
		if (Session.State == TEXT("Failed"))
		{
			FRPGDungeonFlowTestAccess::TravelFailure(Flow, World);
			TestEqual(TEXT("Lobby load failure releases the guard with a safe code"), Flow->GetLastErrorCode(), FString(TEXT("lobby_travel_failed")));
		}
		else
		{
			FRPGDungeonFlowTestAccess::SimulateDestinationLoaded(Flow, World);
			TestEqual(TEXT("Real destination completion releases the guard"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Idle);
			TestEqual(TEXT("Recovery reason survives destination load"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_connection_closed")));
		}
		TestFalse(TEXT("Recovery completion releases the input guard"), Flow->IsRequestInFlight());
		TestEqual(TEXT("Backend state still controls the active character lease"), Flow->HasActiveDungeonSession(),
			Session.State == TEXT("InProgress") || Session.State == TEXT("SettlementPending"));
		Context.TravelURL.Reset();
	}
	Session.State = TEXT("InProgress");
	for (const TCHAR* InvalidMap : {TEXT("127.0.0.1:7777"), TEXT("/Game/Maps/LobbyMap?JoinTicket=invalid"), TEXT("/Game/Maps/NonexistentRecoveryFixture")})
	{
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		FRPGDungeonFlowTestAccess::SetMap(Flow, InvalidMap);
		TestFalse(TEXT("Emergency routing cannot bypass destination validation"), Flow->DisconnectToLobby(Controller));
		TestTrue(TEXT("Invalid recovery destination never schedules travel"), Context.TravelURL.IsEmpty());
	}
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestFalse(TEXT("Recovery needs an own local controller"), Flow->DisconnectToLobby(nullptr));
	Session.Members.Reset();
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestFalse(TEXT("Recovery cannot use unrelated session membership"), Flow->DisconnectToLobby(Controller));
	Session.Members.Add(Member);
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	FRPGDungeonFlowTestAccess::TravelFailure(Flow, World);
	TestEqual(TEXT("Failed dungeon connection schedules local recovery"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	FRPGDungeonFlowTestAccess::RunRecovery(Flow);
	FRPGDungeonFlowTestAccess::SimulateDestinationLoaded(Flow, World);
	TestEqual(TEXT("Connection failure exposes no raw engine URL"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_connection_failed")));
	Context.TravelURL.Reset();
	// Exercise the bounded wait without invoking RPC/packet dispatch in this fixture.
	UIpNetDriver* Driver = NewObject<UIpNetDriver>();
	UIpConnection* Connection = NewObject<UIpConnection>(Driver);
	Driver->SetWorld(World); World->SetNetDriver(Driver);
	Driver->ServerConnection = Connection; Connection->Driver = Driver;
	Connection->SetConnectionState(USOCK_Open);
	ON_SCOPE_EXIT
	{
		Connection->Driver = nullptr; Driver->ServerConnection = nullptr;
		World->SetNetDriver(nullptr); Driver->SetWorld(nullptr);
	};
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestTrue(TEXT("Local recovery can be queued before the close wait"), Flow->DisconnectToLobby(Controller));
	FRPGDungeonFlowTestAccess::WaitForClose(Flow, Connection);
	TestTrue(TEXT("Open current server socket delays local travel"), FRPGDungeonFlowTestAccess::RunRecovery(Flow));
	TestTrue(TEXT("Waiting cannot destroy the actor channel by local travel"), Context.TravelURL.IsEmpty());
	Connection->SetConnectionState(USOCK_Closed);
	TestFalse(TEXT("Server close releases the wait"), FRPGDungeonFlowTestAccess::RunRecovery(Flow));
	TestEqual(TEXT("Close leads only to the configured lobby"), Context.TravelURL, FString(TEXT("/Game/Maps/LobbyMap")));
	Context.TravelURL.Reset();
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestTrue(TEXT("Another explicit recovery can start after reset"), Flow->DisconnectToLobby(Controller));
	Connection->SetConnectionState(USOCK_Open);
	FRPGDungeonFlowTestAccess::WaitForClose(Flow, Connection, true);
	TestFalse(TEXT("Unresponsive server cannot indefinitely trap local recovery"), FRPGDungeonFlowTestAccess::RunRecovery(Flow));
	TestEqual(TEXT("Expired wait still uses safe absolute lobby travel"), Context.TravelURL, FString(TEXT("/Game/Maps/LobbyMap")));
	Context.TravelURL.Reset();
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	TestTrue(TEXT("Third recovery queued for replacement guard"), Flow->DisconnectToLobby(Controller));
	FRPGDungeonFlowTestAccess::WaitForClose(Flow, Connection);
	Driver->ServerConnection = NewObject<UIpConnection>(Driver);
	TestFalse(TEXT("Old planned return stops when the connection changes"), FRPGDungeonFlowTestAccess::RunRecovery(Flow));
	TestTrue(TEXT("Old planned return cannot close a replacement through local travel"), Context.TravelURL.IsEmpty());
	TestEqual(TEXT("Replacement failure exposes only a safe code"), Flow->GetLastErrorCode(), FString(TEXT("lobby_recovery_connection_changed")));
	Driver->ServerConnection = Connection;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPendingConnectionRecoveryTest,
	"ProjectRPG.Online.DungeonPendingConnectionRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPendingConnectionRecoveryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Pending recovery world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>();
	Context.OwningGameInstance = Instance;
	World->SetGameInstance(Instance);
	URPGDungeonSessionSubsystem* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	UIpNetDriver* Driver = NewObject<UIpNetDriver>();
	Driver->NetDriverName = NAME_PendingNetDriver;
	Driver->ServerConnection = NewObject<UIpConnection>(Driver);
	Context.PendingNetGame = NewObject<UPendingNetGame>();
	Context.PendingNetGame->NetDriver = Driver;
	ON_SCOPE_EXIT
	{
		FRPGDungeonFlowTestAccess::CancelRecovery(Flow);
		Context.PendingNetGame = nullptr;
		Context.TravelURL.Reset();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("own-session");
	Session.State = TEXT("InProgress");
	Session.ServerId = TEXT("own-server");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	TestNull(TEXT("Pending driver has no world before admission"), Driver->GetWorld());
	TestEqual(TEXT("Pending driver is a client"), Driver->GetNetMode(), NM_Client);
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Null-world failure resolves its owning context"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	TestTrue(TEXT("Recovery holds input until destination load"), Flow->IsRequestInFlight());
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Repeated pending failure cannot cancel recovery"), Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	FRPGDungeonFlowTestAccess::RunRecovery(Flow);
	TestEqual(TEXT("Recovery schedules only the configured lobby"), Context.TravelURL, FString(TEXT("/Game/Maps/LobbyMap")));
	TestEqual(TEXT("Recovery strips server URL and ticket"), Context.TravelType, TRAVEL_Absolute);
	FRPGDungeonFlowTestAccess::SimulateDestinationLoaded(Flow, World);
	TestEqual(TEXT("Only safe failure reason reaches UI"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_connection_lost")));
	TestFalse(TEXT("Destination load releases input"), Flow->IsRequestInFlight());
	Context.TravelURL.Reset();

	// The player may still be standing in the lobby when admission is rejected.
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::SetMap(Flow, World->GetOutermost()->GetName());
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Admission rejection in lobby releases Connecting"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Error);
	TestEqual(TEXT("Lobby rejection uses a safe code"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_connection_lost")));
	TestTrue(TEXT("Lobby admission failure does not reload the map"), Context.TravelURL.IsEmpty());
	TestFalse(TEXT("Lobby admission failure releases input"), Flow->IsRequestInFlight());
	TestEqual(TEXT("Session survives admission rejection"), Flow->GetCurrentDungeonSession().DungeonSessionId, Session.DungeonSessionId);
	TestEqual(TEXT("Server identity survives admission rejection"), Flow->GetCurrentDungeonSession().ServerId, Session.ServerId);
	TestEqual(TEXT("Unfinished membership survives rejection"), Flow->GetCurrentDungeonSession().Members.Num(), 1);
	TestEqual(TEXT("Character selection survives rejection"), Flow->GetSelectedCharacterId(), Member.CharacterId);
	TestTrue(TEXT("Admission rejection is not abandonment"), Flow->HasActiveDungeonSession());
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	FRPGDungeonFlowTestAccess::TravelFailure(Flow, World);
	TestEqual(TEXT("Travel failure in lobby also releases Connecting"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Error);
	TestEqual(TEXT("Travel failure does not expose the raw error"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_connection_failed")));
	TestTrue(TEXT("Travel failure in lobby never schedules another map"), Context.TravelURL.IsEmpty());
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::SetMap(Flow, TEXT("/Game/Maps/NonexistentRecoveryFixture"));
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Unavailable recovery map cannot leave Connecting stuck"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Error);
	TestTrue(TEXT("Unavailable recovery map never schedules travel"), Context.TravelURL.IsEmpty());

	// Same driver name is insufficient: stale, foreign, non-client and unrelated
	// events must not move this client or release another request's guard.
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginConnecting(Flow);
	UIpNetDriver* StaleDriver = NewObject<UIpNetDriver>();
	StaleDriver->NetDriverName = NAME_PendingNetDriver;
	StaleDriver->ServerConnection = NewObject<UIpConnection>(StaleDriver);
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, StaleDriver);
	TestEqual(TEXT("Inactive pending driver is ignored"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
	Context.OwningGameInstance = NewObject<UGameInstance>();
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Foreign GameInstance is ignored"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
	Context.OwningGameInstance = Instance;
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, NewObject<UWorld>(), Driver);
	TestEqual(TEXT("Foreign supplied world is ignored"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
	Driver->ServerConnection = nullptr;
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Server-role failure is ignored"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
	Driver->ServerConnection = NewObject<UIpConnection>(Driver);
	Driver->NetDriverName = NAME_GameNetDriver;
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Null-world game driver is not a pending admission"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
	Driver->NetDriverName = NAME_PendingNetDriver;
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::NetworkFailure(Flow, nullptr, Driver);
	TestEqual(TEXT("Late admission failure cannot interrupt a session"), Flow->GetFlowState(), ERPGDungeonClientFlowState::InSession);
	TestTrue(TEXT("Ignored failures never schedule travel"), Context.TravelURL.IsEmpty());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonJoinTicketDenialFlowTest,
	"ProjectRPG.Online.DungeonJoinTicketFailure.Flow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonJoinTicketDenialFlowTest::RunTest(const FString&)
{
	auto* Flow = NewObject<URPGDungeonSessionSubsystem>(NewObject<UGameInstance>());
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("ticket-session");
	Session.State = TEXT("InProgress");
	FRPGDungeonSessionMember Member; Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);
	for (const FString Code : { FString(TEXT("dungeon_reconnect_grace_expired")), FString(TEXT("dungeon_member_not_active")) })
	{
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		FRPGDungeonFlowTestAccess::BeginTicket(Flow);
		FRPGDungeonFlowTestAccess::RejectTicket(Flow, TEXT("other-session"), Member.CharacterId, Code);
		TestTrue(TEXT("Foreign session cannot complete the pending request"), Flow->IsRequestInFlight());
		FRPGDungeonFlowTestAccess::RejectTicket(Flow, Session.DungeonSessionId, TEXT("other-character"), Code);
		TestTrue(TEXT("Foreign character cannot complete the pending request"), Flow->IsRequestInFlight());
		FRPGDungeonFlowTestAccess::RejectTicket(Flow, Session.DungeonSessionId, Member.CharacterId, Code);
		TestFalse(TEXT("Denial releases busy UI without travel"), Flow->IsRequestInFlight());
		TestEqual(TEXT("BP receives the exact known denial through existing flow state"), Flow->GetLastErrorCode(), Code);
		TestEqual(TEXT("Denial preserves selected character"), Flow->GetSelectedCharacterId(), Member.CharacterId);
		TestEqual(TEXT("Denial is not a membership deletion/abandonment receipt"), Flow->GetCurrentDungeonSession().Members.Num(), 1);
	}
	FRPGDungeonFlowTestAccess::Seed(Flow, Session);
	FRPGDungeonFlowTestAccess::BeginTicket(Flow);
	FRPGDungeonFlowTestAccess::RejectTicket(Flow, Session.DungeonSessionId, Member.CharacterId, TEXT("untrusted body"));
	TestEqual(TEXT("Unknown denial remains a generic failure"), Flow->GetLastErrorCode(), FString(TEXT("join_ticket_or_travel_failed")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonRejectedAdmissionRecoveryTest,
	"ProjectRPG.Online.DungeonJoinTicketFailure.MembershipRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonRejectedAdmissionRecoveryTest::RunTest(const FString&)
{
	auto* Flow = NewObject<URPGDungeonSessionSubsystem>(NewObject<UGameInstance>());
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("expired-session"); Session.State = TEXT("InProgress");
	FRPGDungeonSessionMember Member; Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);
	const FString Expired = TEXT("dungeon_reconnect_grace_expired");
	const auto Deny = [&](const FString& Code)
	{
		FRPGDungeonFlowTestAccess::Seed(Flow, Session);
		FRPGDungeonFlowTestAccess::BeginTicket(Flow);
		FRPGDungeonFlowTestAccess::RejectTicket(Flow, Session.DungeonSessionId, Member.CharacterId, Code);
	};
	Deny(Expired);
	TestEqual(TEXT("Denial is retained independently of the latest UI error"), Flow->GetDungeonAdmissionBlockReason(), Expired);
	TestFalse(TEXT("A second BP entry cannot request another ticket"), Flow->ConnectToDungeon(nullptr, TEXT("127.0.0.1:7777")));
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, FRPGDungeonSession(), false);
	TestEqual(TEXT("Failed/unauthorized read preserves the old session"), Flow->GetCurrentDungeonSession().DungeonSessionId, Session.DungeonSessionId);
	TestEqual(TEXT("Failed read keeps the entry guard"), Flow->GetDungeonAdmissionBlockReason(), Expired);
	TestTrue(TEXT("Failed read never grants a new dungeon"), Flow->HasActiveDungeonSession());
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, Session);
	TestEqual(TEXT("Pending backend expulsion cannot restore expired entry"), Flow->GetDungeonAdmissionBlockReason(), Expired);
	TestEqual(TEXT("Refresh keeps the specific expiry message"), Flow->GetLastErrorCode(), Expired);
	FRPGDungeonSession Foreign = Session; Foreign.Members[0].CharacterId = TEXT("other-character");
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, Foreign);
	TestEqual(TEXT("Foreign membership is not evidence to discard the old session"), Flow->GetCurrentDungeonSession().Members[0].CharacterId, Member.CharacterId);
	TestEqual(TEXT("Foreign membership leaves the guard intact"), Flow->GetDungeonAdmissionBlockReason(), Expired);
	// The HTTP layer maps only a successful authenticated owner-active 204 to this empty result.
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, FRPGDungeonSession());
	TestFalse(TEXT("Verified no-active membership releases the stale instance"), Flow->HasActiveDungeonSession());
	TestTrue(TEXT("Verified absence clears the UI-only entry guard"), Flow->GetDungeonAdmissionBlockReason().IsEmpty());
	TestEqual(TEXT("Recovery preserves character selection"), Flow->GetSelectedCharacterId(), Member.CharacterId);
	TestEqual(TEXT("Recovery ends idle without travel or abandonment"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Idle);
	Deny(Expired);
	FRPGDungeonSession NewSession = Session; NewSession.DungeonSessionId = TEXT("new-active-session");
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, NewSession);
	TestEqual(TEXT("An owned newer instance can be restored"), Flow->GetCurrentDungeonSession().DungeonSessionId, NewSession.DungeonSessionId);
	TestTrue(TEXT("Old instance guard cannot block a newer instance"), Flow->GetDungeonAdmissionBlockReason().IsEmpty());
	Deny(TEXT("dungeon_member_not_active"));
	FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow);
	FRPGDungeonFlowTestAccess::Receive(Flow, Session);
	TestTrue(TEXT("Fresh owned active membership can resolve a non-expiry conflict"), Flow->GetDungeonAdmissionBlockReason().IsEmpty());
	TestEqual(TEXT("Resolved membership is ready for a new validated ticket"), Flow->GetFlowState(), ERPGDungeonClientFlowState::InSession);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDepartureRecoveryTest, "ProjectRPG.Online.DungeonDepartureRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonDepartureRecoveryTest::RunTest(const FString& Parameters)
{
	UGameInstance* Instance = NewObject<UGameInstance>();
	URPGDungeonSessionSubsystem* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonSession Base;
	TestFalse(TEXT("Retry cannot create a fresh departure without a captured command"), Flow->RetryDeparture());
	TestFalse(TEXT("No uncertainty is invented by a rejected retry"), Flow->HasUnresolvedDeparture());
	Base.DungeonSessionId = TEXT("own-session"); Base.State = TEXT("Waiting");
	Base.LeaderCharacterId = TEXT("own-character"); Base.LobbyRevision = 11;
	Base.DepartureRequestId = TEXT("original-command"); Base.DepartureRequestRevision = 10;
	Base.DepartureRequestedAt = TEXT("confirmed");
	FRPGDungeonSessionMember Member; Member.CharacterId = TEXT("own-character"); Base.Members.Add(Member);
	auto Begin = [&](bool bCancel) { FRPGDungeonFlowTestAccess::BeginDepartureRecovery(Flow, Base, bCancel); };
	auto Receive = [&](const FRPGDungeonSession& Session) { FRPGDungeonFlowTestAccess::Receive(Flow, Session); };
	Begin(true); Receive(Base);
	TestTrue(TEXT("Same active command is not evidence that an uncertain cancel failed"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	TestTrue(TEXT("Uncertainty is exposed to the lobby without exposing credentials"), Flow->HasUnresolvedDeparture());
	TestFalse(TEXT("No-world retry stays rejected instead of bypassing the lobby/auth guard"), Flow->RetryDeparture());
	TestTrue(TEXT("Rejected retry preserves the original command"), Flow->HasUnresolvedDeparture());
	Begin(true); FRPGDungeonSession Loading = Base; Loading.State = TEXT("Loading"); Receive(Loading);
	TestFalse(TEXT("Claim-winning cancellation cannot lock future lobbies"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(true); FRPGDungeonSession Cancelled = Base; Cancelled.DepartureRequestedAt.Reset(); Cancelled.LobbyRevision = 12; Receive(Cancelled);
	TestFalse(TEXT("Confirmed cancellation resolves the original command"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(false); Receive(Base);
	TestFalse(TEXT("Confirmed request resolves a lost request response"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(false); FRPGDungeonSession Unchanged = Base; Unchanged.DepartureRequestedAt.Reset(); Unchanged.DepartureRequestId.Reset(); Unchanged.DepartureRequestRevision = 0; Unchanged.LobbyRevision = 10; Receive(Unchanged);
	TestTrue(TEXT("Unchanged precommit lobby retains the same retry identity"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(true); FRPGDungeonSession NewLobby = Unchanged; NewLobby.DungeonSessionId = TEXT("next-session"); NewLobby.LobbyRevision = 1; Receive(NewLobby);
	TestFalse(TEXT("An authoritative next owned lobby releases the old uncertainty"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(true); FRPGDungeonSession Foreign = Loading; Foreign.Members[0].CharacterId = TEXT("foreign-character"); Receive(Foreign);
	TestTrue(TEXT("Nonmember snapshot cannot discard pending identity"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	Begin(true); FRPGDungeonFlowTestAccess::BeginMembershipRecheck(Flow); Receive(FRPGDungeonSession());
	TestFalse(TEXT("Authenticated no-active-session recovery releases old pending identity"), FRPGDungeonFlowTestAccess::HasPendingDeparture(Flow));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonTravelTargetTest,
	"ProjectRPG.Online.DungeonTravelTarget.Binding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonTravelTargetTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Target test world"), World)) return false;
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	UGameInstance* Instance = NewObject<UGameInstance>();
	World->SetGameInstance(Instance);
	ON_SCOPE_EXIT
	{
		Context.TravelURL.Reset(); // Never tick or execute a scheduled network travel.
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Target test controller"), Controller)) return false;
	Controller->SetAsLocalPlayerController();
	auto* Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
	FRPGDungeonSession Base;
	Base.DungeonSessionId = TEXT("bound-session");
	Base.ServerId = TEXT("bound-server");
	Base.ServerAddress = TEXT("127.0.0.1:7777");
	Base.State = TEXT("InProgress");
	FRPGDungeonSessionMember Member; Member.CharacterId = TEXT("own-character");
	Base.Members.Add(Member);
	for (const TCHAR* Address : { TEXT("192.0.2.1:7777"), TEXT("127.0.0.1:7778"),
		TEXT("127.0.0.1:7777?game=Other"), TEXT("/Game/Maps/LobbyMap") })
	{
		FRPGDungeonFlowTestAccess::Seed(Flow, Base);
		TestFalse(TEXT("A BP caller cannot choose a different ticket destination"), Flow->ConnectToDungeon(Controller, Address));
		TestEqual(TEXT("Address rejected before needing HTTP transport"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_server_address_mismatch")));
		TestFalse(TEXT("Mismatch cannot leave a pending ticket operation"), Flow->IsRequestInFlight());
		TestTrue(TEXT("Mismatch never schedules travel"), Context.TravelURL.IsEmpty());
	}
	FRPGDungeonFlowTestAccess::Seed(Flow, Base);
	TestFalse(TEXT("Matching address still needs the real HTTP service"), Flow->ConnectToDungeon(Controller, TEXT(" 127.0.0.1:7777 ")));
	TestEqual(TEXT("Matching trimmed endpoint reaches the existing transport guard"), Flow->GetLastErrorCode(), FString(TEXT("http_manager_unavailable")));

	for (const TCHAR* Mutation : { TEXT("address"), TEXT("empty-address"), TEXT("state"), TEXT("member"), TEXT("server") })
	{
		FRPGDungeonSession Changed = Base;
		if (FCString::Strcmp(Mutation, TEXT("address")) == 0) Changed.ServerAddress = TEXT("127.0.0.1:7778");
		if (FCString::Strcmp(Mutation, TEXT("empty-address")) == 0) Changed.ServerAddress.Reset();
		if (FCString::Strcmp(Mutation, TEXT("state")) == 0) Changed.State = TEXT("Failed");
		if (FCString::Strcmp(Mutation, TEXT("member")) == 0) Changed.Members.Reset();
		if (FCString::Strcmp(Mutation, TEXT("server")) == 0) Changed.ServerId.Reset();
		FRPGDungeonFlowTestAccess::Seed(Flow, Changed);
		FRPGDungeonFlowTestAccess::PendingTarget(Flow, Controller, Base.ServerAddress);
		FRPGDungeonFlowTestAccess::IssueTicket(Flow, Base.DungeonSessionId, Member.CharacterId);
		TestEqual(TEXT("A late success cannot travel against a changed assignment/member"), Flow->GetLastErrorCode(), FString(TEXT("dungeon_server_assignment_changed")));
		TestTrue(TEXT("Changed assignment never forwards the ticket"), Context.TravelURL.IsEmpty());
		Context.TravelURL.Reset(); // Also isolate pre-fix failure cases.
		TestFalse(TEXT("Late rejection releases the pending operation"), Flow->IsRequestInFlight());
		TestEqual(TEXT("Late rejection preserves the authenticated session snapshot"), Flow->GetCurrentDungeonSession().ServerAddress, Changed.ServerAddress);
	}
	for (const TCHAR* Address : { TEXT("127.0.0.1:7777"), TEXT("[::1]:7777"), TEXT("dungeon.example.test:7777") })
	{
		FRPGDungeonSession Matching = Base; Matching.ServerAddress = Address;
		FRPGDungeonFlowTestAccess::Seed(Flow, Matching);
		FRPGDungeonFlowTestAccess::PendingTarget(Flow, Controller, Matching.ServerAddress);
		FRPGDungeonFlowTestAccess::IssueTicket(Flow, TEXT("foreign-session"), Member.CharacterId);
		TestTrue(TEXT("Foreign response keeps the original request pending"), Flow->IsRequestInFlight());
		TestTrue(TEXT("Foreign response never travels"), Context.TravelURL.IsEmpty());
		FRPGDungeonFlowTestAccess::IssueTicket(Flow, Base.DungeonSessionId, Member.CharacterId);
		TestEqual(TEXT("Same bound target enters Connecting"), Flow->GetFlowState(), ERPGDungeonClientFlowState::Connecting);
		TestEqual(TEXT("Only the bound endpoint receives the ticket"), Context.TravelURL,
			Matching.ServerAddress + TEXT("?JoinTicket=bound-target-fixture-ticket"));
		Context.TravelURL.Reset();
	}
	return true;
}
#endif
