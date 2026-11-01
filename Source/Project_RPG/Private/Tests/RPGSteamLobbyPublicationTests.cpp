#if WITH_DEV_AUTOMATION_TESTS

#include "Online/RPGSteamLobbySubsystem.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/NetworkDelegates.h"
#include "HAL/PlatformTime.h"
#include "Manager/HttpWebManager.h"
#include "Misc/AutomationTest.h"
#include "OnlineSessionSettings.h"
#include "UObject/Package.h"

// Exercise production flow with local fixture identity and a recording OSS; never use HTTP/Steam.
struct FRPGSteamLobbyPublicationTestAccess
{
	static bool Mark(URPGSteamLobbySubsystem* Steam, const FRPGDungeonSession& Session,
		const FRPGSteamLobbyInfo& Lobby, const FString& Character = TEXT("own-character"),
		bool bHost = true, bool bAuthenticated = true, bool bBackendIdle = true)
	{
		return Steam->TryMarkAutomaticServerPublication(Session, Character, Lobby,
			bHost, bAuthenticated, bBackendIdle);
	}
	static FRPGSteamLobbyInfo Publication(const FRPGDungeonSession& Session)
	{
		return URPGSteamLobbySubsystem::MakeServerPublication(Session);
	}
	static FRPGSteamLobbyInfo Attempt(const URPGSteamLobbySubsystem* Steam)
	{
		return Steam->LastServerPublicationAttempt;
	}
	static void Busy(URPGSteamLobbySubsystem* Steam, bool bBusy)
	{
		Steam->PendingOperation = bBusy ? URPGSteamLobbySubsystem::EPendingOperation::Update
			: URPGSteamLobbySubsystem::EPendingOperation::None;
	}
	static void ReceiveUpdate(URPGSteamLobbySubsystem* Steam, bool bSuccess,
		FName SessionName = TEXT("RPGPartyLobby"))
	{
		Steam->HandleUpdateSessionComplete(SessionName, bSuccess);
	}
	static void SetLobby(URPGSteamLobbySubsystem* Steam, const FRPGSteamLobbyInfo& Lobby)
	{
		Steam->CurrentLobby = Lobby;
	}
	static void Clear(URPGSteamLobbySubsystem* Steam) { Steam->ClearCurrentLobby(); }
	static bool Login(UHttpWebManager* Web)
	{
		const double Now = FPlatformTime::Seconds();
		return Web->PlayerAuth.AcceptLogin(TEXT("publication-fixture"), TEXT("76561198000000001"),
			TEXT("2026-10-11T01:00:00Z"), TEXT("2026-10-11T00:00:00Z"), Now, Now);
	}
	static void Seed(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonSession& Session)
	{
		Flow->CurrentDungeonSession = Session;
		Flow->SelectedCharacterId = TEXT("own-character");
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::None;
		Flow->FlowState = ERPGDungeonClientFlowState::InSession;
	}
	static void BackendBusy(URPGDungeonSessionSubsystem* Flow, bool bBusy)
	{
		Flow->PendingOperation = bBusy ? URPGDungeonSessionSubsystem::EPendingOperation::Refresh
			: URPGDungeonSessionSubsystem::EPendingOperation::None;
	}
	static void UnresolvedAbandonment(URPGDungeonSessionSubsystem* Flow, bool bUnresolved)
	{
		Flow->AbandonmentSessionId = bUnresolved ? TEXT("own-session") : TEXT("");
		Flow->AbandonmentCharacterId = bUnresolved ? TEXT("own-character") : TEXT("");
		Flow->AbandonmentSteamId = bUnresolved ? TEXT("76561198000000001") : TEXT("");
	}
	static void BindTransport(URPGSteamLobbySubsystem* Steam, IOnlineSessionPtr Sessions)
	{
		Steam->ClearSessionDelegates();
		Steam->SessionInterface = MoveTemp(Sessions);
		Steam->RegisterSessionDelegates();
	}
	static void Creating(URPGSteamLobbySubsystem* Steam)
	{
		Steam->PendingOperation = URPGSteamLobbySubsystem::EPendingOperation::Create;
	}
};

namespace RPGSteamLobbyPublicationTests
{
	FRPGDungeonSession ReadySession()
	{
		FRPGDungeonSession Session;
		Session.DungeonSessionId = TEXT("own-session");
		Session.State = TEXT("InProgress");
		Session.ServerId = TEXT("assigned-server");
		Session.ServerAddress = TEXT(" 127.0.0.1:18827 ");
		FRPGDungeonSessionMember Member;
		Member.CharacterId = TEXT("own-character");
		Session.Members.Add(Member);
		return Session;
	}
	FRPGSteamLobbyInfo WaitingLobby()
	{
		FRPGSteamLobbyInfo Lobby;
		Lobby.DungeonSessionId = TEXT("own-session");
		Lobby.DungeonState = TEXT("Waiting");
		return Lobby;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationGuardTest,
	"ProjectRPG.Online.SteamPublication.Guards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSteamPublicationGuardTest::RunTest(const FString&)
{
	using Access = FRPGSteamLobbyPublicationTestAccess;
	auto* Steam = NewObject<URPGSteamLobbySubsystem>(NewObject<UGameInstance>());
	const FRPGDungeonSession Ready = RPGSteamLobbyPublicationTests::ReadySession();
	const FRPGSteamLobbyInfo Lobby = RPGSteamLobbyPublicationTests::WaitingLobby();
	TestFalse(TEXT("Party members cannot automatically publish"), Access::Mark(Steam, Ready, Lobby, TEXT("own-character"), false));
	TestFalse(TEXT("Expired/missing player authentication cannot publish"), Access::Mark(Steam, Ready, Lobby, TEXT("own-character"), true, false));
	TestFalse(TEXT("Busy, travel, error or unresolved backend flow cannot publish"), Access::Mark(Steam, Ready, Lobby, TEXT("own-character"), true, true, false));
	TestFalse(TEXT("An empty selection cannot publish"), Access::Mark(Steam, Ready, Lobby, TEXT("")));
	TestFalse(TEXT("Another member/character cannot publish"), Access::Mark(Steam, Ready, Lobby, TEXT("other-character")));
	TestFalse(TEXT("Character comparison is case-sensitive"), Access::Mark(Steam, Ready, Lobby, TEXT("OWN-character")));
	for (const TCHAR* State : {TEXT("Waiting"), TEXT("Loading"), TEXT("SettlementPending"),
		TEXT("Cleared"), TEXT("Failed"), TEXT("Closed"), TEXT("inprogress"), TEXT("")})
	{
		auto Session = Ready;
		Session.State = State;
		TestFalse(FString::Printf(TEXT("%s does not acknowledge readiness"), State), Access::Mark(Steam, Session, Lobby));
	}
	auto Session = Ready;
	Session.DungeonSessionId.Reset();
	TestFalse(TEXT("An empty backend session cannot publish"), Access::Mark(Steam, Session, Lobby));
	auto OtherLobby = Lobby;
	OtherLobby.DungeonSessionId = TEXT("OWN-session");
	TestFalse(TEXT("Lobby/backend session comparison is exact"), Access::Mark(Steam, Ready, OtherLobby));
	Session = Ready;
	Session.Members.Reset();
	TestFalse(TEXT("Missing membership cannot publish"), Access::Mark(Steam, Session, Lobby));
	Session = Ready;
	Session.ServerId = TEXT(" ");
	TestFalse(TEXT("An empty server ID cannot publish"), Access::Mark(Steam, Session, Lobby));
	Session = Ready;
	Session.ServerAddress = TEXT(" \t");
	TestFalse(TEXT("An empty server address cannot publish"), Access::Mark(Steam, Session, Lobby));
	Access::Busy(Steam, true);
	TestFalse(TEXT("An OSS operation defers publication without consuming the attempt"), Access::Mark(Steam, Ready, Lobby));
	Access::Busy(Steam, false);
	TestFalse(TEXT("Already advertised metadata needs no update"), Access::Mark(Steam, Ready, Access::Publication(Ready)));
	TestTrue(TEXT("Rejected guards did not consume the first valid attempt"), Access::Mark(Steam, Ready, Lobby));
	TestEqual(TEXT("The advertised address is normalized"), Access::Attempt(Steam).ServerAddress, FString(TEXT("127.0.0.1:18827")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationDeduplicationTest,
	"ProjectRPG.Online.SteamPublication.Deduplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSteamPublicationDeduplicationTest::RunTest(const FString&)
{
	using Access = FRPGSteamLobbyPublicationTestAccess;
	auto* Steam = NewObject<URPGSteamLobbySubsystem>(NewObject<UGameInstance>());
	auto Ready = RPGSteamLobbyPublicationTests::ReadySession();
	const auto Lobby = RPGSteamLobbyPublicationTests::WaitingLobby();
	TestTrue(TEXT("A ready assignment gets one automatic attempt"), Access::Mark(Steam, Ready, Lobby));
	for (int32 Poll = 0; Poll < 20; ++Poll)
		TestFalse(TEXT("Repeated polling/delegates cannot submit the same update"), Access::Mark(Steam, Ready, Lobby));
	Ready.ServerAddress = TEXT("127.0.0.1:18828");
	Access::Busy(Steam, true);
	TestFalse(TEXT("A changed assignment waits for the in-flight update"), Access::Mark(Steam, Ready, Lobby));
	TestEqual(TEXT("Deferral preserves the previous attempt"), Access::Attempt(Steam).ServerAddress, FString(TEXT("127.0.0.1:18827")));
	Access::ReceiveUpdate(Steam, false);
	TestTrue(TEXT("The deferred new assignment can publish after completion"), Access::Mark(Steam, Ready, Lobby));
	Access::Busy(Steam, true);
	Access::ReceiveUpdate(Steam, false);
	TestFalse(TEXT("A failed update is not retried by polling"), Access::Mark(Steam, Ready, Lobby));
	TestFalse(TEXT("Locally mutated OSS settings do not restart a failed attempt"), Access::Mark(Steam, Ready, Access::Publication(Ready)));
	Ready.ServerId = TEXT("replacement-server");
	TestTrue(TEXT("A new server ID permits a new automatic attempt"), Access::Mark(Steam, Ready, Lobby));
	Ready.ServerAddress = TEXT(" 127.0.0.1:18828 ");
	TestFalse(TEXT("Whitespace alone is not a new assignment"), Access::Mark(Steam, Ready, Lobby));
	Ready.DungeonSessionId = TEXT("next-session");
	TestFalse(TEXT("A different backend session cannot update the old party"), Access::Mark(Steam, Ready, Lobby));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationCallbackTest,
	"ProjectRPG.Online.SteamPublication.CallbacksAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSteamPublicationCallbackTest::RunTest(const FString&)
{
	using Access = FRPGSteamLobbyPublicationTestAccess;
	auto* Steam = NewObject<URPGSteamLobbySubsystem>(NewObject<UGameInstance>());
	const auto Ready = RPGSteamLobbyPublicationTests::ReadySession();
	const auto Lobby = RPGSteamLobbyPublicationTests::WaitingLobby();
	Access::SetLobby(Steam, Lobby);
	TestTrue(TEXT("Seed a production attempt"), Access::Mark(Steam, Ready, Lobby));
	Access::Busy(Steam, true);
	Access::ReceiveUpdate(Steam, false, TEXT("unrelated-lobby"));
	TestTrue(TEXT("An unrelated callback cannot release the pending update"), Steam->IsLobbyRequestInFlight());
	Access::ReceiveUpdate(Steam, false);
	TestFalse(TEXT("Failure releases only the pending OSS operation"), Steam->IsLobbyRequestInFlight());
	TestEqual(TEXT("Failure remains visible"), Steam->GetLastErrorCode(), FString(TEXT("update_lobby_failed")));
	TestEqual(TEXT("Failure preserves party/session identity"), Steam->GetCurrentLobby().DungeonSessionId, Lobby.DungeonSessionId);
	TestFalse(TEXT("Failure leaves automatic retry suppressed"), Access::Mark(Steam, Ready, Lobby));
	Access::Busy(Steam, true); // Simulate completion of an explicit manual retry; no transport is invoked.
	Access::ReceiveUpdate(Steam, true);
	TestTrue(TEXT("A successful retry returns to the lobby"), Steam->GetLobbyState() == ERPGSteamLobbyState::InLobby);
	TestTrue(TEXT("A successful retry clears the error"), Steam->GetLastErrorCode().IsEmpty());
	TestFalse(TEXT("Success does not make polling send duplicates"), Access::Mark(Steam, Ready, Lobby));
	Access::Clear(Steam);
	TestTrue(TEXT("Clearing the party clears the old attempt"), Access::Attempt(Steam).DungeonSessionId.IsEmpty());
	TestTrue(TEXT("A recreated matching party can advertise again"), Access::Mark(Steam, Ready, Lobby));
	return true;
}

namespace RPGSteamLobbyPublicationTests
{
	// Only named-session lookup and UpdateSession are implemented. Any other OSS call fails the test.
	class FRecordingSessions final : public IOnlineSession
	{
	public:
		FRecordingSessions()
		{
			FOnlineSessionSettings Settings;
			Settings.NumPublicConnections = 4;
			Settings.Set(FName(TEXT("RPG_DUNGEON_SESSION_ID")), FString(TEXT("own-session")));
			Settings.Set(FName(TEXT("RPG_DUNGEON_STATE")), FString(TEXT("Waiting")));
			Settings.Set(FName(TEXT("RPG_DUNGEON_ID")), FString(TEXT("Dungeon.RaidSwordMan")));
			Lobby = MakeUnique<FNamedOnlineSession>(FName(TEXT("RPGPartyLobby")), Settings);
			Lobby->bHosting = true;
		}
		FNamedOnlineSession* GetNamedSession(FName Name) override
		{ return Name == Lobby->SessionName ? Lobby.Get() : nullptr; }
		bool UpdateSession(FName Name, FOnlineSessionSettings& Settings, bool bRefresh) override
		{
			++UpdateCalls;
			LastName = Name; LastSettings = Settings; bLastRefresh = bRefresh;
			// Steam may mutate local settings even if the online update fails.
			Lobby->SessionSettings = Settings;
			return bAcceptUpdate;
		}
		void Complete(bool bSuccess, FName Name = TEXT("RPGPartyLobby"))
		{ TriggerOnUpdateSessionCompleteDelegates(Name, bSuccess); }
		TUniquePtr<FNamedOnlineSession> Lobby;
		FOnlineSessionSettings LastSettings;
		FName LastName;
		int32 UpdateCalls = 0;
		mutable int32 UnexpectedCalls = 0;
		bool bAcceptUpdate = true;
		bool bLastRefresh = false;

		FNamedOnlineSession* AddNamedSession(FName, const FOnlineSessionSettings&) override { Reject(); return nullptr; }
		FNamedOnlineSession* AddNamedSession(FName, const FOnlineSession&) override { Reject(); return nullptr; }
		FUniqueNetIdPtr CreateSessionIdFromString(const FString&) override { Reject(); return nullptr; }
		void RemoveNamedSession(FName) override { Reject(); }
		bool HasPresenceSession() override { return Reject(); }
		EOnlineSessionState::Type GetSessionState(FName) const override { Reject(); return EOnlineSessionState::NoSession; }
		bool CreateSession(int32, FName, const FOnlineSessionSettings&) override { return Reject(); }
		bool CreateSession(const FUniqueNetId&, FName, const FOnlineSessionSettings&) override { return Reject(); }
		bool StartSession(FName) override { return Reject(); }
		bool EndSession(FName) override { return Reject(); }
		bool DestroySession(FName, const FOnDestroySessionCompleteDelegate&) override { return Reject(); }
		bool IsPlayerInSession(FName, const FUniqueNetId&) override { return Reject(); }
		bool StartMatchmaking(const TArray<FUniqueNetIdRef>&, FName, const FOnlineSessionSettings&, TSharedRef<FOnlineSessionSearch>&) override { return Reject(); }
		bool CancelMatchmaking(int32, FName) override { return Reject(); }
		bool CancelMatchmaking(const FUniqueNetId&, FName) override { return Reject(); }
		bool FindSessions(int32, const TSharedRef<FOnlineSessionSearch>&) override { return Reject(); }
		bool FindSessions(const FUniqueNetId&, const TSharedRef<FOnlineSessionSearch>&) override { return Reject(); }
		bool FindSessionById(const FUniqueNetId&, const FUniqueNetId&, const FUniqueNetId&, const FOnSingleSessionResultCompleteDelegate&) override { return Reject(); }
		bool CancelFindSessions() override { return Reject(); }
		bool PingSearchResults(const FOnlineSessionSearchResult&) override { return Reject(); }
		bool JoinSession(int32, FName, const FOnlineSessionSearchResult&) override { return Reject(); }
		bool JoinSession(const FUniqueNetId&, FName, const FOnlineSessionSearchResult&) override { return Reject(); }
		bool FindFriendSession(int32, const FUniqueNetId&) override { return Reject(); }
		bool FindFriendSession(const FUniqueNetId&, const FUniqueNetId&) override { return Reject(); }
		bool FindFriendSession(const FUniqueNetId&, const TArray<FUniqueNetIdRef>&) override { return Reject(); }
		bool SendSessionInviteToFriend(int32, FName, const FUniqueNetId&) override { return Reject(); }
		bool SendSessionInviteToFriend(const FUniqueNetId&, FName, const FUniqueNetId&) override { return Reject(); }
		bool SendSessionInviteToFriends(int32, FName, const TArray<FUniqueNetIdRef>&) override { return Reject(); }
		bool SendSessionInviteToFriends(const FUniqueNetId&, FName, const TArray<FUniqueNetIdRef>&) override { return Reject(); }
		bool GetResolvedConnectString(FName, FString&, FName) override { return Reject(); }
		bool GetResolvedConnectString(const FOnlineSessionSearchResult&, FName, FString&) override { return Reject(); }
		FOnlineSessionSettings* GetSessionSettings(FName) override { Reject(); return nullptr; }
		bool RegisterPlayer(FName, const FUniqueNetId&, bool) override { return Reject(); }
		bool RegisterPlayers(FName, const TArray<FUniqueNetIdRef>&, bool) override { return Reject(); }
		bool UnregisterPlayer(FName, const FUniqueNetId&) override { return Reject(); }
		bool UnregisterPlayers(FName, const TArray<FUniqueNetIdRef>&) override { return Reject(); }
		void RegisterLocalPlayer(const FUniqueNetId&, FName, const FOnRegisterLocalPlayerCompleteDelegate&) override { Reject(); }
		void UnregisterLocalPlayer(const FUniqueNetId&, FName, const FOnUnregisterLocalPlayerCompleteDelegate&) override { Reject(); }
		int32 GetNumSessions() override { Reject(); return 0; }
		void DumpSessionState() override { Reject(); }
	private:
		bool Reject() const { ++UnexpectedCalls; return false; }
	};

	struct FDispatchFixture
	{
		decltype(FNetDelegates::OnReceivedNetworkEncryptionToken) OriginalToken = FNetDelegates::OnReceivedNetworkEncryptionToken;
		decltype(FNetDelegates::OnReceivedNetworkEncryptionAck) OriginalAck = FNetDelegates::OnReceivedNetworkEncryptionAck;
		decltype(FNetDelegates::OnReceivedNetworkEncryptionFailure) OriginalFailure = FNetDelegates::OnReceivedNetworkEncryptionFailure;
		bool bClosed = false;
		UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
		UHttpWebManager* Web = nullptr;
		URPGDungeonSessionSubsystem* Flow = nullptr;
		URPGSteamLobbySubsystem* Steam = nullptr;
		TSharedPtr<FRecordingSessions, ESPMode::ThreadSafe> Sessions = MakeShared<FRecordingSessions, ESPMode::ThreadSafe>();
		FDispatchFixture()
		{
			Instance->Init();
			Web = Instance->GetSubsystem<UHttpWebManager>();
			Flow = Instance->GetSubsystem<URPGDungeonSessionSubsystem>();
			Steam = Instance->GetSubsystem<URPGSteamLobbySubsystem>();
			if (Web && Flow && Steam)
			{
				FRPGSteamLobbyPublicationTestAccess::Seed(Flow, ReadySession());
				FRPGSteamLobbyPublicationTestAccess::BindTransport(Steam, Sessions);
			}
		}
		~FDispatchFixture() { Close(); }
		void Close()
		{
			if (bClosed) return;
			Instance->Shutdown();
			FNetDelegates::OnReceivedNetworkEncryptionToken = OriginalToken;
			FNetDelegates::OnReceivedNetworkEncryptionAck = OriginalAck;
			FNetDelegates::OnReceivedNetworkEncryptionFailure = OriginalFailure;
			bClosed = true;
		}
		bool BindingsRestored() const
		{
			return FNetDelegates::OnReceivedNetworkEncryptionToken.GetHandle() == OriginalToken.GetHandle()
				&& FNetDelegates::OnReceivedNetworkEncryptionAck.GetHandle() == OriginalAck.GetHandle()
				&& FNetDelegates::OnReceivedNetworkEncryptionFailure.GetHandle() == OriginalFailure.GetHandle();
		}
		void ReadyEvent() { Flow->OnFlowStateChanged.Broadcast(Flow->GetFlowState(), FString()); }
		bool IsValid() const { return Web && Flow && Steam; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationDispatchTest,
	"ProjectRPG.Online.SteamPublication.Dispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSteamPublicationDispatchTest::RunTest(const FString&)
{
	using namespace RPGSteamLobbyPublicationTests;
	using Access = FRPGSteamLobbyPublicationTestAccess;
	FDispatchFixture Fixture;
	if (!TestTrue(TEXT("Real GameInstance subsystem dependencies are available"), Fixture.IsValid())) return false;
	Fixture.ReadyEvent();
	TestEqual(TEXT("Missing authentication cannot dispatch"), Fixture.Sessions->UpdateCalls, 0);
	TestTrue(TEXT("Fixture login uses production expiry/identity state"), Access::Login(Fixture.Web));
	Access::BackendBusy(Fixture.Flow, true); Fixture.ReadyEvent();
	TestEqual(TEXT("A pending backend request cannot dispatch"), Fixture.Sessions->UpdateCalls, 0);
	Access::BackendBusy(Fixture.Flow, false);
	Access::UnresolvedAbandonment(Fixture.Flow, true); Fixture.ReadyEvent();
	TestEqual(TEXT("An unresolved abandonment cannot dispatch"), Fixture.Sessions->UpdateCalls, 0);
	Access::UnresolvedAbandonment(Fixture.Flow, false);
	Fixture.ReadyEvent();
	TestEqual(TEXT("Backend flow delegate dispatches exactly one OSS update"), Fixture.Sessions->UpdateCalls, 1);
	TestEqual(TEXT("Update targets the existing named lobby"), Fixture.Sessions->LastName, FName(TEXT("RPGPartyLobby")));
	TestTrue(TEXT("Update requests online metadata refresh"), Fixture.Sessions->bLastRefresh);
	for (const auto& Pair : TMap<FName, FString>{
		{TEXT("RPG_DUNGEON_SESSION_ID"), TEXT("own-session")},
		{TEXT("RPG_DUNGEON_STATE"), TEXT("InProgress")},
		{TEXT("RPG_SERVER_ID"), TEXT("assigned-server")},
		{TEXT("RPG_SERVER_ADDRESS"), TEXT("127.0.0.1:18827")},
		{TEXT("RPG_DUNGEON_ID"), TEXT("Dungeon.RaidSwordMan")}})
	{
		FString Actual;
		TestTrue(TEXT("Expected metadata is present"), Fixture.Sessions->LastSettings.Get(Pair.Key, Actual));
		TestEqual(Pair.Key.ToString(), Actual, Pair.Value);
		if (Pair.Key == TEXT("RPG_DUNGEON_STATE") || Pair.Key == TEXT("RPG_SERVER_ID") || Pair.Key == TEXT("RPG_SERVER_ADDRESS"))
		{
			const auto* Setting = Fixture.Sessions->LastSettings.Settings.Find(Pair.Key);
			TestTrue(TEXT("Published readiness and server fields are advertised online"),
				Setting && Setting->AdvertisementType == EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
		}
	}
	TestEqual(TEXT("Existing party capacity is preserved"), Fixture.Sessions->LastSettings.NumPublicConnections, 4);
	for (int32 Poll = 0; Poll < 5; ++Poll) Fixture.ReadyEvent();
	TestEqual(TEXT("Repeated delegates cannot duplicate an in-flight update"), Fixture.Sessions->UpdateCalls, 1);
	Fixture.Sessions->Complete(true);
	TestEqual(TEXT("Registered OSS callback refreshes current lobby"), Fixture.Steam->GetCurrentLobby().ServerAddress, FString(TEXT("127.0.0.1:18827")));
	TestFalse(TEXT("Successful completion releases the request"), Fixture.Steam->IsLobbyRequestInFlight());
	Fixture.ReadyEvent();
	TestEqual(TEXT("Success and later polling cannot dispatch duplicates"), Fixture.Sessions->UpdateCalls, 1);
	TestEqual(TEXT("No unrelated OSS operations were invoked"), Fixture.Sessions->UnexpectedCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationDeferredDispatchTest,
	"ProjectRPG.Online.SteamPublication.DeferredDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSteamPublicationDeferredDispatchTest::RunTest(const FString&)
{
	using namespace RPGSteamLobbyPublicationTests;
	using Access = FRPGSteamLobbyPublicationTestAccess;
	FDispatchFixture Fixture;
	if (!TestTrue(TEXT("Real subsystem fixture is available"), Fixture.IsValid())) return false;
	TestTrue(TEXT("Fixture is authenticated"), Access::Login(Fixture.Web));
	Access::Creating(Fixture.Steam); Fixture.ReadyEvent();
	TestEqual(TEXT("Ready event waits for pending lobby creation"), Fixture.Sessions->UpdateCalls, 0);
	Fixture.Sessions->TriggerOnCreateSessionCompleteDelegates(TEXT("RPGPartyLobby"), true);
	TestEqual(TEXT("Registered create completion dispatches the deferred assignment"), Fixture.Sessions->UpdateCalls, 1);
	auto Replacement = ReadySession();
	Replacement.ServerId = TEXT("replacement-server"); Replacement.ServerAddress = TEXT("127.0.0.1:18828");
	Access::Seed(Fixture.Flow, Replacement); Fixture.ReadyEvent();
	TestEqual(TEXT("Replacement waits for the pending update"), Fixture.Sessions->UpdateCalls, 1);
	Fixture.Sessions->Complete(false, TEXT("unrelated-lobby"));
	TestTrue(TEXT("Unrelated completion does not release the pending update"), Fixture.Steam->IsLobbyRequestInFlight());
	Fixture.Sessions->Complete(false);
	TestEqual(TEXT("Failed old update dispatches the changed assignment once"), Fixture.Sessions->UpdateCalls, 2);
	Fixture.Sessions->Complete(true); Fixture.ReadyEvent();
	TestEqual(TEXT("Replacement metadata is refreshed from the named session"), Fixture.Steam->GetCurrentLobby().ServerId, Replacement.ServerId);
	TestEqual(TEXT("Replacement success does not trigger another update"), Fixture.Sessions->UpdateCalls, 2);
	TestEqual(TEXT("No unrelated OSS operations were invoked"), Fixture.Sessions->UnexpectedCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSteamPublicationManualRetryDispatchTest,
	"ProjectRPG.Online.SteamPublication.ManualRetryDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSteamPublicationManualRetryDispatchTest::RunTest(const FString&)
{
	using namespace RPGSteamLobbyPublicationTests;
	using Access = FRPGSteamLobbyPublicationTestAccess;
	FDispatchFixture Fixture;
	if (!TestTrue(TEXT("Real subsystem fixture is available"), Fixture.IsValid())) return false;
	TestTrue(TEXT("Fixture is authenticated"), Access::Login(Fixture.Web));
	Fixture.Sessions->bAcceptUpdate = false; Fixture.ReadyEvent();
	TestEqual(TEXT("Synchronous rejection is visible"), Fixture.Steam->GetLastErrorCode(), FString(TEXT("update_lobby_request_rejected")));
	TestFalse(TEXT("Synchronous rejection releases the request"), Fixture.Steam->IsLobbyRequestInFlight());
	Fixture.ReadyEvent();
	TestEqual(TEXT("Rejected metadata is not retried by polling"), Fixture.Sessions->UpdateCalls, 1);
	TestTrue(TEXT("Failure preserves the party"), Fixture.Steam->IsInPartyLobby());
	TestEqual(TEXT("Failure preserves backend membership"), Fixture.Flow->GetCurrentDungeonSession().Members.Num(), 1);
	Fixture.Sessions->bAcceptUpdate = true;
	TestTrue(TEXT("Public manual retry dispatches even with locally mutated settings"), Fixture.Steam->PublishDungeonServerAddress());
	TestEqual(TEXT("Manual retry reached OSS"), Fixture.Sessions->UpdateCalls, 2);
	Fixture.Sessions->Complete(false); Fixture.ReadyEvent();
	TestEqual(TEXT("Asynchronous failure is visible"), Fixture.Steam->GetLastErrorCode(), FString(TEXT("update_lobby_failed")));
	TestEqual(TEXT("Asynchronous failure does not loop"), Fixture.Sessions->UpdateCalls, 2);
	TestTrue(TEXT("Another explicit retry remains available"), Fixture.Steam->PublishDungeonServerAddress());
	Fixture.Sessions->Complete(true); Fixture.ReadyEvent();
	TestTrue(TEXT("Successful retry returns to InLobby"), Fixture.Steam->GetLobbyState() == ERPGSteamLobbyState::InLobby);
	TestTrue(TEXT("Successful retry clears the error"), Fixture.Steam->GetLastErrorCode().IsEmpty());
	TestEqual(TEXT("Only the three explicit attempts reached OSS"), Fixture.Sessions->UpdateCalls, 3);
	TestEqual(TEXT("No unrelated OSS operations were invoked"), Fixture.Sessions->UnexpectedCalls, 0);
	Fixture.Close();
	TestTrue(TEXT("Fixture cleanup restores original global encryption delegates"), Fixture.BindingsRestored());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
