#if WITH_DEV_AUTOMATION_TESTS
#include "Manager/HttpWebManager.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include <limits>

struct FRPGBackendAuthTestAccess
{
	static void PendingLogin(UHttpWebManager* Web, const FHttpRequestPtr& Request, double AgeSeconds = 0.0)
	{
		Web->ActiveAuthenticationRequest = Request;
		Web->ActiveAuthenticationRequestStartedSeconds = FPlatformTime::Seconds() - AgeSeconds;
	}
	static bool AcceptResponse(UHttpWebManager* Web, const FHttpRequestPtr& Request,
		const FHttpResponsePtr& Response, bool& bSuccessful)
	{ return Web->AcceptPlayerResponse(Request, Response, bSuccessful); }
	static void SetBusy(URPGSteamAuthSubsystem* Auth) { Auth->bLoginRequestInFlight = true; }
	static void TicketResponse(UHttpWebManager* Web, const FHttpRequestPtr& Request,
		const FHttpResponsePtr& Response, bool bSuccess = true, uint64 Generation = 0)
	{
		Web->OnRequestJoinTicketResponseReceived(Request, Response, bSuccess,
			TEXT("own-character"), TEXT("ticket-session"), Generation);
	}
	static bool Login(UHttpWebManager* Web, const FString& Token, const FString& SteamId)
	{
		const FDateTime Issued = FDateTime::UtcNow();
		const double Now = FPlatformTime::Seconds();
		return Web->PlayerAuth.AcceptLogin(Token, SteamId, (Issued + FTimespan::FromMinutes(5)).ToIso8601(),
			Issued.ToIso8601(), Now, Now);
	}
	static void BeginLogout(UHttpWebManager* Web, const FHttpRequestRef& Request, bool bContextValid,
		TFunction<void(bool)> Completion)
	{
		Request->SetHeader(TEXT("Authorization"), Web->PlayerAuth.LogoutAuthorization());
		Web->ActivePlayerLogoutRequest = Request;
		Web->PlayerLogoutContextValidator = [bContextValid] { return bContextValid; };
		Web->PlayerLogoutCompletion = MoveTemp(Completion);
	}
	static void LogoutResponse(UHttpWebManager* Web, const FHttpRequestPtr& Request,
		const FHttpResponsePtr& Response, bool bSuccessful = true)
	{ Web->OnPlayerLogoutResponse(Request, Response, bSuccessful); }
	static void ReplaceCredential(UHttpWebManager* Web) { Web->PlayerAuth.SetOpaqueToken(TEXT("replacement-fixture")); }
};

namespace
{
	class FAuthResponse final : public IHttpResponse
	{
	public:
		FAuthResponse(int32 InCode, const FString& InBody = FString()) : Code(InCode), Body(InBody)
		{
			const FTCHARToUTF8 Utf8(*Body);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		virtual const FString& GetURL() const override { return Url; }
		virtual const FString& GetEffectiveURL() const override { return Url; }
		virtual EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		virtual EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
		virtual FString GetURLParameter(const FString&) const override { return FString(); }
		virtual FString GetHeader(const FString&) const override { return FString(); }
		virtual TArray<FString> GetAllHeaders() const override { return {}; }
		virtual FString GetContentType() const override { return TEXT("application/json"); }
		virtual uint64 GetContentLength() const override { return Bytes.Num(); }
		virtual const TArray<uint8>& GetContent() const override { return Bytes; }
		virtual TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		virtual int32 GetResponseCode() const override { return Code; }
		virtual FString GetContentAsString() const override { return Body; }
		virtual FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Code;
		FString Url = TEXT("http://auth-fixture.invalid/api");
		FString Body;
		TArray<uint8> Bytes;
	};
	FHttpResponsePtr Response(int32 Code, const FString& Body = FString())
	{ return MakeShared<FAuthResponse, ESPMode::ThreadSafe>(Code, Body); }
	FString LoginBody(const FString& Token, const FString& SteamId, const FDateTime& IssuedAt = FDateTime::UtcNow())
	{
		return FString::Printf(TEXT("{\"accessToken\":\"%s\",\"steamId\":\"%s\",\"expiresAt\":\"%s\",\"issuedAt\":\"%s\"}"),
			*Token, *SteamId, *(IssuedAt + FTimespan::FromHours(1)).ToIso8601(), *IssuedAt.ToIso8601());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBackendPlayerAuthenticationTest,
	"ProjectRPG.Online.DungeonAuthentication", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBackendPlayerAuthenticationTest::RunTest(const FString&)
{
	const FString Account = TEXT("76561198000000001");
	const FString Other = TEXT("76561198000000002");
	const FDateTime IssuedAt(2026, 10, 7, 0, 0, 0);
	const FString ExpiresAt = (IssuedAt + FTimespan::FromMinutes(5)).ToIso8601();
	const double Now = 1000.0;
	const double Expires = Now + 300.0;
	FRPGBackendPlayerAuthState State;
	TestFalse(TEXT("New state is unauthenticated"), State.HasAccessToken(Now));
	TestTrue(TEXT("Valid login binds the account and expiry"), State.AcceptLogin(TEXT("fixture-first"), Account, ExpiresAt, IssuedAt.ToIso8601(), Now, Now));
	FRPGBackendPlayerAuthState Boundary = State;
	TestTrue(TEXT("Credential works just before expiry"), Boundary.HasAccessToken(Expires - 0.001));
	TestFalse(TEXT("Exact expiry is unauthenticated"), Boundary.HasAccessToken(Expires));
	TestTrue(TEXT("Expired credential sends no authorization"), Boundary.Authorization(Expires).IsEmpty());
	TestEqual(TEXT("Expiry retains account identity"), State.GetSteamId(), Account);
	for (const FString BadExpiry : TArray<FString>{FString(), TEXT("not-a-date"), TEXT("2026-10-07T00:05:00"), TEXT("2026-10-07"), IssuedAt.ToIso8601()})
		TestFalse(TEXT("Missing, ambiguous or expired metadata is rejected"), State.AcceptLogin(TEXT("bad"), Account, BadExpiry, IssuedAt.ToIso8601(), Now, Now));
	for (const FString BadToken : TArray<FString>{FString(), TEXT("bad token"), TEXT("bad\r\nheader")})
		TestFalse(TEXT("Unsafe header tokens are rejected"), State.AcceptLogin(BadToken, Account, ExpiresAt, IssuedAt.ToIso8601(), Now, Now));
	TestFalse(TEXT("A different account cannot replace the pinned identity"), State.AcceptLogin(TEXT("foreign"), Other, ExpiresAt, IssuedAt.ToIso8601(), Now, Now));
	TestFalse(TEXT("Rejected responses leave the original credential intact"), State.InvalidateIfUnauthorized(TEXT("Bearer foreign"), 401));
	TestTrue(TEXT("Failed renewal retains the still-valid token"), State.HasAccessToken(Now));
	TestFalse(TEXT("403 is not an authentication expiry"), State.InvalidateIfUnauthorized(TEXT("Bearer fixture-first"), 403));
	TestTrue(TEXT("Current request 401 invalidates credential"), State.InvalidateIfUnauthorized(TEXT("Bearer fixture-first"), 401));
	TestFalse(TEXT("401 makes login available"), State.HasAccessToken(Now));
	TestEqual(TEXT("401 retains the account binding"), State.GetSteamId(), Account);
	TestTrue(TEXT("Outstanding requests remain correlated after 401"), State.MatchesRequest(TEXT("Bearer fixture-first")));
	TestTrue(TEXT("Same-account renewal is allowed"), State.AcceptLogin(TEXT("fixture-new"), Account, TEXT("2026-10-07T09:05:00+09:00"), IssuedAt.ToIso8601(), Now, Now));
	TestFalse(TEXT("Late 401 from the old credential is ignored"), State.InvalidateIfUnauthorized(TEXT("Bearer fixture-first"), 401));
	TestTrue(TEXT("Late 401 does not erase the new credential"), State.HasAccessToken(Now));
	State.Invalidate();
	TestEqual(TEXT("Explicit credential clearing is not account switching"), State.GetSteamId(), Account);
	TestFalse(TEXT("Steam ID format is ASCII-only"), FRPGBackendPlayerAuthState::IsSteamId(TEXT("7656119800000000x")));
	State.Reset();
	TestTrue(TEXT("Lifetime reset clears identity"), State.GetSteamId().IsEmpty());
	State.SetOpaqueToken(TEXT(" legacy-fixture "));
	TestTrue(TEXT("Legacy setter remains supported without expiry metadata"), State.HasAccessToken(Now));
	State.SetOpaqueToken(TEXT("bad\nheader"));
	TestFalse(TEXT("Legacy setter cannot introduce a header injection"), State.HasAccessToken(Now));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBackendAuthenticationClockTest,
	"ProjectRPG.Online.DungeonAuthenticationClock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBackendAuthenticationClockTest::RunTest(const FString&)
{
	const FString Account = TEXT("76561198000000001");
	const double Started = 1000.0;
	const double Received = 1002.0;
	const FDateTime ServerIssued(2000, 1, 1);
	const FString IssuedAt = ServerIssued.ToIso8601();
	const FString ExpiresAt = (ServerIssued + FTimespan::FromMinutes(5)).ToIso8601();
	FRPGBackendPlayerAuthState State;
	TestTrue(TEXT("Past server timestamps are valid without comparing to client UTC"),
		State.AcceptLogin(TEXT("clock-fixture"), Account, ExpiresAt, IssuedAt, Started, Received));
	TestTrue(TEXT("Request/response time is charged to the lifetime"), State.HasAccessToken(1299.999));
	TestFalse(TEXT("Deadline uses send time, not receive time plus lifetime"), State.HasAccessToken(1300.0));
	TestFalse(TEXT("Backward readings cannot resurrect an expired token"), State.HasAccessToken(1100.0));
	TestTrue(TEXT("Expired token sends no header even after backward readings"), State.Authorization(1100.0).IsEmpty());
	TestEqual(TEXT("Local expiry keeps the account pinned"), State.GetSteamId(), Account);
	TestTrue(TEXT("Expiry retains correlation for outstanding failures"), State.MatchesRequest(TEXT("Bearer clock-fixture")));
	TestTrue(TEXT("A fresh same-account login resets the monotonic deadline"),
		State.AcceptLogin(TEXT("clock-renewed"), Account, ExpiresAt, IssuedAt, 1305.0, 1307.0));
	TestTrue(TEXT("Renewed token is available"), State.HasAccessToken(1308.0));
	TestFalse(TEXT("Old 401 cannot erase the renewed deadline"), State.InvalidateIfUnauthorized(TEXT("Bearer clock-fixture"), 401));
	TestFalse(TEXT("A regressing monotonic counter fails closed"), State.HasAccessToken(1307.0));
	TestFalse(TEXT("A later valid-looking reading does not undo the anomaly"), State.HasAccessToken(1309.0));

	for (const double Bad : { -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
	{
		FRPGBackendPlayerAuthState Invalid;
		TestFalse(TEXT("Invalid request timestamps are rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, IssuedAt, Bad, Received));
		TestFalse(TEXT("Invalid receipt timestamps are rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, IssuedAt, Started, Bad));
	}
	FRPGBackendPlayerAuthState Invalid;
	TestFalse(TEXT("Receipt before request is rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, IssuedAt, Started, Started - 1.0));
	TestFalse(TEXT("A response that used the entire lifetime is rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, IssuedAt, Started, Started + 300.0));
	TestFalse(TEXT("Missing issuedAt cannot fall back to client UTC"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, FString(), Started, Received));
	TestFalse(TEXT("Unzoned issuedAt is rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, ExpiresAt, TEXT("2000-01-01T00:00:00"), Started, Received));
	TestFalse(TEXT("Expiry before issue is rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account, IssuedAt, ExpiresAt, Started, Received));
	TestFalse(TEXT("Lifetime greater than backend maximum is rejected"), Invalid.AcceptLogin(TEXT("fixture"), Account,
		(ServerIssued + FTimespan::FromHours(24) + FTimespan::FromSeconds(1)).ToIso8601(), IssuedAt, Started, Received));
	TestTrue(TEXT("Exact backend maximum is accepted"), Invalid.AcceptLogin(TEXT("fixture"), Account,
		(ServerIssued + FTimespan::FromHours(24)).ToIso8601(), IssuedAt, Started, Received));
	TestFalse(TEXT("Non-finite runtime time fails closed"), Invalid.HasAccessToken(std::numeric_limits<double>::quiet_NaN()));
	TestFalse(TEXT("Runtime anomaly is sticky"), Invalid.HasAccessToken(Received));
	FRPGBackendPlayerAuthState Future;
	const FDateTime FutureIssued(2200, 1, 1);
	TestTrue(TEXT("Future server timestamps also use only relative lifetime"), Future.AcceptLogin(TEXT("future-fixture"), Account,
		(FutureIssued + FTimespan::FromMinutes(5)).ToIso8601(), FutureIssued.ToIso8601(), Started, Received));
	TestFalse(TEXT("Future UTC metadata cannot extend the monotonic deadline"), Future.HasAccessToken(Started + 300.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBackendAuthenticationTransportTest,
	"ProjectRPG.Online.DungeonAuthenticationTransport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBackendAuthenticationTransportTest::RunTest(const FString&)
{
	const FString Account = TEXT("76561198000000001");
	auto* GameInstance = NewObject<UGameInstance>();
	auto* Web = NewObject<UHttpWebManager>(GameInstance);
	const auto Login = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Login);
	Web->OnAuthenticateResponseReceived(Login, Response(200, LoginBody(TEXT("fixture-first"), Account, FDateTime(2000, 1, 1))), true);
	TestTrue(TEXT("Transport accepts token, identity and expiry metadata"), Web->HasAccessToken());
	TestEqual(TEXT("Transport exposes only the safe account ID"), Web->GetAuthenticatedSteamId(), Account);
	const auto Malformed = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Malformed);
	Web->OnAuthenticateResponseReceived(Malformed, Response(200, TEXT("{\"accessToken\":\"incomplete\"}")), true);
	TestTrue(TEXT("Missing expiry/account metadata cannot erase a valid credential"), Web->HasAccessToken());
	const auto MissingIssued = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, MissingIssued);
	Web->OnAuthenticateResponseReceived(MissingIssued, Response(200, FString::Printf(
		TEXT("{\"accessToken\":\"missing-issued\",\"steamId\":\"%s\",\"expiresAt\":\"2200-01-01T01:00:00Z\"}"), *Account)), true);
	TestTrue(TEXT("Missing issuedAt preserves the valid credential without wall-clock fallback"), Web->HasAccessToken());
	const auto Delayed = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Delayed, 7200.0);
	Web->OnAuthenticateResponseReceived(Delayed, Response(200, LoginBody(TEXT("too-late"), Account)), true);
	TestTrue(TEXT("Delayed authentication cannot replace the valid credential"), Web->HasAccessToken());
	const auto Old = FHttpModule::Get().CreateRequest();
	Web->ApplyCommonHeaders(Old);
	bool bSuccessful = true;
	TestTrue(TEXT("Current 403 completes as a normal failure"), FRPGBackendAuthTestAccess::AcceptResponse(Web, Old, Response(403), bSuccessful));
	TestTrue(TEXT("403 preserves credential"), Web->HasAccessToken());
	TestTrue(TEXT("Current 401 is delivered to release the pending operation"), FRPGBackendAuthTestAccess::AcceptResponse(Web, Old, Response(401), bSuccessful));
	TestFalse(TEXT("401 is not a successful operation"), bSuccessful);
	TestFalse(TEXT("Transport invalidates the token"), Web->HasAccessToken());
	TestEqual(TEXT("Transport keeps the account for re-login"), Web->GetAuthenticatedSteamId(), Account);
	bSuccessful = true;
	TestTrue(TEXT("Concurrent response with invalidated credential still completes"), FRPGBackendAuthTestAccess::AcceptResponse(Web, Old, Response(200, TEXT("[]")), bSuccessful));
	TestFalse(TEXT("Concurrent success cannot apply data after credential loss"), bSuccessful);
	const auto NoHeader = FHttpModule::Get().CreateRequest();
	bSuccessful = true;
	TestTrue(TEXT("Expiry between guard/header completes without leaving UI busy"), FRPGBackendAuthTestAccess::AcceptResponse(Web, NoHeader, Response(401), bSuccessful));
	TestFalse(TEXT("Missing-header expiry cannot apply data"), bSuccessful);
	const auto View = URPGOnlineLobbyWidget::BuildPresentation(true, Web->HasAccessToken(), false, false,
		false, false, true, TEXT("selected"), FRPGDungeonSession(), ERPGDungeonClientFlowState::InSession);
	TestTrue(TEXT("Expired lobby enables login"), View.bCanLogin);
	TestFalse(TEXT("Expired lobby blocks new sessions"), View.bCanCreateSession);
	const auto Renew = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Renew);
	Web->AuthenticateWithSteamTicket(TEXT(""));
	// Duplicate auth must leave the actual outstanding request alive.
	Web->OnAuthenticateResponseReceived(Renew, Response(200, LoginBody(TEXT("fixture-new"), Account)), true);
	TestTrue(TEXT("Same-account renewal succeeds despite a duplicate call"), Web->HasAccessToken());
	bSuccessful = true;
	TestFalse(TEXT("Previous credential 401 is not delivered into new UI state"), FRPGBackendAuthTestAccess::AcceptResponse(Web, Old, Response(401), bSuccessful));
	TestFalse(TEXT("Previous credential success is also stale"), FRPGBackendAuthTestAccess::AcceptResponse(Web, Old, Response(200, TEXT("[]")), bSuccessful));
	TestTrue(TEXT("New token survives stale callbacks"), Web->HasAccessToken());
	bSuccessful = true;
	TestFalse(TEXT("Late headerless response is ignored after re-login"), FRPGBackendAuthTestAccess::AcceptResponse(Web, NoHeader, Response(401), bSuccessful));
	TestTrue(TEXT("Late headerless response cannot invalidate renewal"), Web->HasAccessToken());
	Web->OnAuthenticateResponseReceived(Login, Response(200, LoginBody(TEXT("stale-auth"), Account)), true);
	const auto Current = FHttpModule::Get().CreateRequest();
	Web->ApplyCommonHeaders(Current);
	TestEqual(TEXT("A stale authentication callback cannot overwrite renewal"), Current->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer fixture-new")));
	const auto Foreign = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Foreign);
	Web->OnAuthenticateResponseReceived(Foreign, Response(200, LoginBody(TEXT("foreign"), TEXT("76561198000000002"))), true);
	TestEqual(TEXT("HTTP 200 cannot implicitly switch accounts"), Web->GetAuthenticatedSteamId(), Account);
	TestTrue(TEXT("Failed account switch retains the current credential"), Web->HasAccessToken());
	const auto Future = FHttpModule::Get().CreateRequest();
	FRPGBackendAuthTestAccess::PendingLogin(Web, Future);
	Web->OnAuthenticateResponseReceived(Future, Response(200, LoginBody(TEXT("fixture-future"), Account, FDateTime(2200, 1, 1))), true);
	TestTrue(TEXT("Transport accepts server-relative lifetime despite future wall-clock metadata"), Web->HasAccessToken());
	const auto PreDisconnect = FHttpModule::Get().CreateRequest();
	Web->ApplyCommonHeaders(PreDisconnect);
	Web->InvalidatePlayerDungeonRequests();
	Web->OnDungeonSessionResponseReceived(PreDisconnect, Response(401), true, TEXT("load"), TEXT("fixture-session"), 0);
	TestTrue(TEXT("Pre-disconnect session response cannot invalidate the retained credential"), Web->HasAccessToken());
	Web->OnRequestJoinTicketResponseReceived(PreDisconnect, Response(401), true, TEXT("fixture-character"), TEXT("fixture-session"), 0);
	TestTrue(TEXT("Pre-disconnect ticket response cannot invalidate the retained credential"), Web->HasAccessToken());
	Web->OnDungeonSessionResponseReceived(PreDisconnect, Response(401), true, TEXT("load"), TEXT("fixture-session"), 1);
	TestFalse(TEXT("Current-generation 401 still invalidates authentication normally"), Web->HasAccessToken());
	TestEqual(TEXT("Local recovery never resets the pinned account"), Web->GetAuthenticatedSteamId(), Account);
	auto* Auth = NewObject<URPGSteamAuthSubsystem>(GameInstance);
	FRPGBackendAuthTestAccess::SetBusy(Auth);
	Auth->LoginToBackendWithSteam();
	TestTrue(TEXT("Duplicate Steam login does not clear the in-flight guard"), Auth->IsLoginRequestInFlight());
	Auth->LoginToDevelopmentBackend(Account);
	TestTrue(TEXT("Duplicate development login does not clear the guard"), Auth->IsLoginRequestInFlight());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonJoinTicketDenialTransportTest,
	"ProjectRPG.Online.DungeonJoinTicketFailure.Transport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonJoinTicketDenialTransportTest::RunTest(const FString&)
{
	auto* Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	Web->SetAccessToken(TEXT("ticket-fixture"));
	const auto Request = FHttpModule::Get().CreateRequest();
	Web->ApplyCommonHeaders(Request);
	int32 Completed = 0;
	FString ReceivedCode;
	Web->OnJoinTicketResult.AddLambda([&](const FString& Session, const FString& Character,
		const FString& Ticket, const FString& Expiry, bool bSuccess, const FString& ErrorCode)
	{
		++Completed;
		ReceivedCode = ErrorCode;
		TestEqual(TEXT("Request session stays correlated"), Session, FString(TEXT("ticket-session")));
		TestEqual(TEXT("Request character stays correlated"), Character, FString(TEXT("own-character")));
		TestFalse(TEXT("Denial is never a usable ticket"), bSuccess);
		TestTrue(TEXT("Denial clears ticket and expiry"), Ticket.IsEmpty() && Expiry.IsEmpty());
	});
	const FString Body = TEXT("{\"error\":\"dungeon_reconnect_grace_expired\"}");
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(409, Body));
	TestEqual(TEXT("Known conflict reaches flow"), ReceivedCode, FString(TEXT("dungeon_reconnect_grace_expired")));
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(409, TEXT("{\"error\":\"dungeon_member_not_active\"}")));
	TestEqual(TEXT("Nonmember conflict is distinct"), ReceivedCode, FString(TEXT("dungeon_member_not_active")));
	for (const FString InvalidBody : { FString(TEXT("{\"error\":\"DUNGEON_RECONNECT_GRACE_EXPIRED\"}")),
		FString(TEXT("{\"error\":\"arbitrary backend text\"}")), FString(TEXT("invalid json")),
		Body + FString::ChrN(4096, TEXT(' ')) })
	{
		FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(409, InvalidBody));
		TestTrue(TEXT("Unknown/malformed/oversized errors are not authority"), ReceivedCode.IsEmpty());
	}
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(500, Body));
	TestTrue(TEXT("Server failure cannot impersonate expiry"), ReceivedCode.IsEmpty());
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(200, Body));
	TestTrue(TEXT("Malformed success cannot impersonate expiry"), ReceivedCode.IsEmpty());
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(409, Body), false);
	TestTrue(TEXT("Transport failure cannot impersonate expiry"), ReceivedCode.IsEmpty());
	const int32 BeforeStale = Completed;
	FRPGBackendAuthTestAccess::TicketResponse(Web, Request, Response(409, Body), true, 99);
	const auto ForeignRequest = FHttpModule::Get().CreateRequest();
	ForeignRequest->SetHeader(TEXT("Authorization"), TEXT("Bearer old-fixture"));
	FRPGBackendAuthTestAccess::TicketResponse(Web, ForeignRequest, Response(409, Body));
	TestEqual(TEXT("Old generation/token never changes current UI"), Completed, BeforeStale);
	TestTrue(TEXT("Denials do not clear authentication"), Web->HasAccessToken());
	Web->OnJoinTicketResult.Clear();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPlayerLogoutTransportTest,
	"ProjectRPG.Online.DungeonLogout.Transport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPlayerLogoutTransportTest::RunTest(const FString&)
{
	auto* Web = NewObject<UHttpWebManager>(NewObject<UGameInstance>());
	const FString FirstAccount = TEXT("76561198000000001"), NextAccount = TEXT("76561198000000002");
	TestTrue(TEXT("Initial account authenticated"), FRPGBackendAuthTestAccess::Login(Web, TEXT("logout-fixture"), FirstAccount));
	int32 Completed = 0; bool bResult = false;
	const auto Begin = [&](bool bContextValid = true)
	{
		const auto Request = FHttpModule::Get().CreateRequest();
		FRPGBackendAuthTestAccess::BeginLogout(Web, Request, bContextValid,
			[&](bool bSuccess) { ++Completed; bResult = bSuccess; });
		return Request;
	};
	for (int32 Code : { 200, 202, 403, 500 })
	{
		const auto Request = Begin();
		TestFalse(TEXT("Logout blocks other authenticated work"), Web->HasAccessToken());
		const auto OtherRequest = FHttpModule::Get().CreateRequest(); Web->ApplyCommonHeaders(OtherRequest);
		TestTrue(TEXT("Other requests cannot send player auth during logout"), OtherRequest->GetHeader(TEXT("Authorization")).IsEmpty());
		Web->SetAccessToken(TEXT("must-not-replace"));
		TestFalse(TEXT("Duplicate logout cannot replace the original callback"),
			Web->RequestPlayerLogout([] { return true; }, [](bool) {}));
		FRPGBackendAuthTestAccess::LogoutResponse(Web, Request, Response(Code));
		TestFalse(TEXT("Only 204/401 confirm logout"), bResult);
		TestTrue(TEXT("Failure restores access to the existing credential"), Web->HasAccessToken());
		TestEqual(TEXT("Failure keeps account pinned"), Web->GetAuthenticatedSteamId(), FirstAccount);
	}
	const auto Lost = Begin();
	FRPGBackendAuthTestAccess::LogoutResponse(Web, Lost, Response(204), false);
	TestFalse(TEXT("Transport failure cannot authorize account switching"), bResult);
	TestEqual(TEXT("Lost acknowledgement retains account"), Web->GetAuthenticatedSteamId(), FirstAccount);
	const auto ChangedWorld = Begin(false);
	FRPGBackendAuthTestAccess::LogoutResponse(Web, ChangedWorld, Response(204));
	TestFalse(TEXT("Changed lobby context cannot reset identity"), bResult);
	TestFalse(TEXT("Known revoked credential is no longer usable"), Web->HasAccessToken());
	TestEqual(TEXT("Changed context keeps account binding"), Web->GetAuthenticatedSteamId(), FirstAccount);
	TestTrue(TEXT("Retained correlation supports an expired/revoked-token retry"), Web->HasPlayerLogoutCredential());
	const auto Retry = Begin();
	FRPGBackendAuthTestAccess::LogoutResponse(Web, Retry, Response(401));
	TestTrue(TEXT("Confirmed invalid credential completes same-token retry"), bResult);
	TestTrue(TEXT("Confirmed logout clears account binding"), Web->GetAuthenticatedSteamId().IsEmpty());
	TestFalse(TEXT("Confirmed logout clears the credential"), Web->HasPlayerLogoutCredential());
	TestTrue(TEXT("Another account requires and may follow explicit confirmed logout"),
		FRPGBackendAuthTestAccess::Login(Web, TEXT("next-account-fixture"), NextAccount));
	const auto Stale = Begin();
	FRPGBackendAuthTestAccess::ReplaceCredential(Web);
	FRPGBackendAuthTestAccess::LogoutResponse(Web, Stale, Response(204));
	TestFalse(TEXT("Old credential receipt cannot clear a replacement"), bResult);
	TestTrue(TEXT("Replacement survives old logout acknowledgement"), Web->HasAccessToken());
	TestEqual(TEXT("Replacement account binding survives"), Web->GetAuthenticatedSteamId(), NextAccount);
	const auto Current = Begin();
	const int32 BeforeForeign = Completed;
	FRPGBackendAuthTestAccess::LogoutResponse(Web, FHttpModule::Get().CreateRequest(), Response(204));
	TestEqual(TEXT("Foreign request does not finish current logout"), Completed, BeforeForeign);
	FRPGBackendAuthTestAccess::LogoutResponse(Web, Current, Response(204));
	TestTrue(TEXT("Matching acknowledgement completes logout"), bResult);
	FRPGBackendAuthTestAccess::LogoutResponse(Web, Current, Response(204));
	TestEqual(TEXT("Duplicate receipt completes at most once"), Completed, BeforeForeign + 1);
	return true;
}
#endif
