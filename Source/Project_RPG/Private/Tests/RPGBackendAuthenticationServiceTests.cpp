#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGBackendAuthentication.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

struct FRPGBackendAuthenticationServiceTestAccess
{
	static void Age(FRPGBackendAuthentication& Auth, double Seconds)
	{ Auth.LoginStartedSeconds = FPlatformTime::Seconds() - Seconds; }
};

namespace RPGAuthenticationServiceTests
{
	class FResponse final : public IHttpResponse
	{
	public:
		FResponse(int32 InCode, FString InBody) : Code(InCode), Body(MoveTemp(InBody))
		{
			const FTCHARToUTF8 Utf8(*Body);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		const FString& GetURL() const override { return Url; }
		const FString& GetEffectiveURL() const override { return Url; }
		EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
		FString GetURLParameter(const FString&) const override { return {}; }
		FString GetHeader(const FString&) const override { return {}; }
		TArray<FString> GetAllHeaders() const override { return {}; }
		FString GetContentType() const override { return TEXT("application/json"); }
		uint64 GetContentLength() const override { return Bytes.Num(); }
		const TArray<uint8>& GetContent() const override { return Bytes; }
		TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		int32 GetResponseCode() const override { return Code; }
		FString GetContentAsString() const override { return Body; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Code;
		FString Body, Url = TEXT("http://auth-service-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	struct FHost final : IRPGBackendAuthenticationHost
	{
		FRPGBackendPlayerAuthState State;
		FRPGBackendAuthenticationConfig Config{TEXT("http://auth-service-fixture.invalid/api"), 7.f, false, false};
		int32 LoginReplies = 0, Observations = 0, Invalidations = 0;
		bool bLoginSucceeded = false;
		TFunction<void()> OnLogin;
		UObject& GetAuthenticationOwner() const override { return *GetTransientPackage(); }
		FRPGBackendAuthenticationConfig GetAuthenticationConfig() const override { return Config; }
		void ApplyAuthenticationHeaders(const FHttpRequestRef& Request) const override
		{
			Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
			const FString Authorization = State.Authorization(FPlatformTime::Seconds());
			if (!Authorization.IsEmpty()) Request->SetHeader(TEXT("Authorization"), Authorization);
		}
		void ObserveAuthenticationResponse(const FHttpRequestPtr&, const FHttpResponsePtr&) override { ++Observations; }
		void BroadcastAuthenticationResult(bool bSuccess) override
		{ ++LoginReplies; bLoginSucceeded = bSuccess; if (OnLogin) OnLogin(); }
		void InvalidateAuthenticationDungeonRequests() override { ++Invalidations; }
	};
	const FString Account = TEXT("76561198000000001");
	FString LoginBody(const FString& Id = Account)
	{
		const FDateTime Issued(2000, 1, 1);
		return FString::Printf(TEXT("{\"accessToken\":\"auth-service-token-fixture\",\"steamId\":\"%s\",\"expiresAt\":\"%s\",\"issuedAt\":\"%s\"}"),
			*Id, *(Issued + FTimespan::FromMinutes(5)).ToIso8601(), *Issued.ToIso8601());
	}
	void Seed(FHost& Host)
	{
		const FDateTime Issued(2000, 1, 1);
		const double Now = FPlatformTime::Seconds();
		Host.State.AcceptLogin(TEXT("auth-service-seed-fixture"), Account,
			(Issued + FTimespan::FromMinutes(5)).ToIso8601(), Issued.ToIso8601(), Now, Now);
	}
	void Complete(const FHttpRequestPtr& Request, int32 Status, const FString& Body = FString())
	{
		Request->OnProcessRequestComplete().ExecuteIfBound(Request,
			MakeShared<FResponse, ESPMode::ThreadSafe>(Status, Body), true);
	}
	FHttpRequestPtr Logout(const TSharedRef<FRPGBackendAuthentication>& Auth, TFunction<void(bool)> Completion)
	{ return Auth->CreateLogoutRequest([] { return true; }, MoveTemp(Completion)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceLoginTest, "ProjectRPG.Online.AuthenticationService.Login",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceLoginTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host;
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	const auto First = Auth->CreateLoginRequest(TEXT("aabb"));
	if (!TestTrue(TEXT("Login reserves a real build-only HTTP request"), First.IsValid())) return false;
	TestEqual(TEXT("Same login endpoint"), First->GetURL(), Host.Config.ApiUrl / TEXT("auth/steam-ticket"));
	TestEqual(TEXT("Login stays POST"), First->GetVerb(), FString(TEXT("POST")));
	const auto& Bytes = First->GetContent();
	const FUTF8ToTCHAR Converted(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
	TestEqual(TEXT("Original ticket wire body"), FString(Converted.Length(), Converted.Get()), FString(TEXT("{\"ticket\":\"aabb\"}")));
	Auth->Authenticate(TEXT(""));
	TestEqual(TEXT("Duplicate empty login cannot notify/cancel the original"), Host.LoginReplies, 0);
	FHttpRequestPtr Next;
	Host.OnLogin = [&] { Next = Auth->CreateLoginRequest(TEXT("ccdd")); };
	Complete(First, 200, LoginBody());
	Host.OnLogin = nullptr;
	if (!TestTrue(TEXT("Login slot released before notification/reentry"), Next.IsValid())) return false;
	TestTrue(TEXT("Server-relative lifetime accepted"), Auth->HasAccessToken());
	TestEqual(TEXT("Account remains pinned"), Host.State.GetSteamId(), Account);
	Complete(First, 200, LoginBody());
	TestEqual(TEXT("Old callback never notifies twice"), Host.LoginReplies, 1);
	TestEqual(TEXT("Old callback never observes drain into a new request"), Host.Observations, 1);
	TestTrue(TEXT("Old callback cannot clear replacement login"), Auth->IsLoginActive());
	Complete(Next, 200, LoginBody());
	TestEqual(TEXT("Current login completes"), Host.LoginReplies, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceValidationTest, "ProjectRPG.Online.AuthenticationService.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceValidationTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host; Seed(Host);
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	const FString Original = Host.State.Authorization(FPlatformTime::Seconds());
	AddExpectedError(TEXT("Steam backend authentication failed"), EAutomationExpectedErrorFlags::Contains, 3);
	const TArray<FString> Bodies{TEXT("{\"accessToken\":\"incomplete-fixture\"}"), LoginBody(TEXT("76561198000000002")), LoginBody()};
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const auto Request = Auth->CreateLoginRequest(TEXT("fixture"));
		if (!Request) return false;
		if (Index == 2) FRPGBackendAuthenticationServiceTestAccess::Age(*Auth, 301.0);
		Complete(Request, 200, Bodies[Index]);
		TestFalse(TEXT("Incomplete, foreign-account or expired-in-flight login fails"), Host.bLoginSucceeded);
		TestEqual(TEXT("Failure preserves current credential"), Host.State.Authorization(FPlatformTime::Seconds()), Original);
		TestEqual(TEXT("Failure preserves account binding"), Host.State.GetSteamId(), Account);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceLogoutCodesTest, "ProjectRPG.Online.AuthenticationService.LogoutCodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceLogoutCodesTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host; Seed(Host);
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	int32 Replies = 0; bool bResult = false;
	for (int32 Code : {200, 202, 403, 500})
	{
		const auto Request = Logout(Auth, [&](bool bSuccess) { ++Replies; bResult = bSuccess; });
		if (!Request) return false;
		TestFalse(TEXT("Logout blocks credential use while pending"), Auth->HasAccessToken());
		Complete(Request, Code);
		TestFalse(TEXT("Only exact 204/401 confirm logout"), bResult);
		TestTrue(TEXT("Unconfirmed logout preserves credential"), Auth->HasAccessToken());
	}
	const auto Confirmed = Logout(Auth, [&](bool bSuccess) { ++Replies; bResult = bSuccess; });
	if (!Confirmed) return false;
	Complete(Confirmed, 204);
	TestTrue(TEXT("204 confirms explicit logout"), bResult);
	TestTrue(TEXT("Confirmed logout clears account binding"), Host.State.GetSteamId().IsEmpty());
	Seed(Host); Host.State.Invalidate();
	const auto Expired = Logout(Auth, [&](bool bSuccess) { ++Replies; bResult = bSuccess; });
	if (!TestTrue(TEXT("Expired token retains its logout credential"), Expired.IsValid())) return false;
	Complete(Expired, 401);
	TestTrue(TEXT("401 confirms revocation for the same credential"), bResult);
	TestEqual(TEXT("Each current completion delivered once"), Replies, 6);
	TestEqual(TEXT("Only confirmed logouts invalidate dungeon callbacks"), Host.Invalidations, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceLogoutScopeTest, "ProjectRPG.Online.AuthenticationService.LogoutScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceLogoutScopeTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host; Seed(Host);
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	bool bResult = true;
	const auto ChangedContext = Auth->CreateLogoutRequest([] { return false; }, [&](bool bSuccess) { bResult = bSuccess; });
	if (!ChangedContext) return false;
	Complete(ChangedContext, 204);
	TestFalse(TEXT("Changed selection/world cannot finalize logout"), bResult);
	TestFalse(TEXT("Revoked credential is unusable"), Auth->HasAccessToken());
	TestEqual(TEXT("Account remains pinned on changed context"), Host.State.GetSteamId(), Account);
	Seed(Host);
	const auto ChangedCredential = Logout(Auth, [&](bool bSuccess) { bResult = bSuccess; });
	if (!ChangedCredential) return false;
	Host.State.SetOpaqueToken(TEXT("replacement-fixture"));
	Complete(ChangedCredential, 204);
	TestFalse(TEXT("Old credential cannot finalize a replacement"), bResult);
	TestEqual(TEXT("Replacement survives old confirmation"), Host.State.Authorization(FPlatformTime::Seconds()), FString(TEXT("Bearer replacement-fixture")));
	TestEqual(TEXT("Neither failure invalidates dungeon callbacks"), Host.Invalidations, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceBarriersTest, "ProjectRPG.Online.AuthenticationService.Barriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceBarriersTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host;
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	TestFalse(TEXT("No credential means no logout"), Logout(Auth, [](bool) {}).IsValid());
	Seed(Host); Host.Config.bServerRuntime = true;
	TestFalse(TEXT("Dedicated server cannot dispatch player logout"), Logout(Auth, [](bool) {}).IsValid());
	Host.Config.bServerRuntime = false; Host.Config.bProgressionPending = true;
	TestFalse(TEXT("Progression write blocks logout"), Logout(Auth, [](bool) {}).IsValid());
	Host.Config.bProgressionPending = false;
	if (!Auth->CreateLoginRequest(TEXT("fixture"))) return false;
	TestFalse(TEXT("Login blocks logout"), Logout(Auth, [](bool) {}).IsValid());
	Auth->CancelLogin();
	if (!Logout(Auth, [](bool) {})) return false;
	TestFalse(TEXT("Logout blocks another logout"), Logout(Auth, [](bool) {}).IsValid());
	TestFalse(TEXT("Logout blocks login"), Auth->CreateLoginRequest(TEXT("fixture")).IsValid());
	TestEqual(TEXT("Denied builds never publish fake login results"), Host.LoginReplies, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceLifetimeTest, "ProjectRPG.Online.AuthenticationService.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceLifetimeTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host;
	TSharedPtr<FRPGBackendAuthentication> Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	const auto First = Auth->CreateLoginRequest(TEXT("fixture"));
	if (!First) return false;
	auto LateLogin = First->OnProcessRequestComplete();
	Auth->CancelLogin();
	const auto Current = Auth->CreateLoginRequest(TEXT("fixture"));
	if (!Current) return false;
	LateLogin.ExecuteIfBound(First, MakeShared<FResponse, ESPMode::ThreadSafe>(200, LoginBody()), true);
	TestTrue(TEXT("Canceled callback cannot clear replacement login"), Auth->IsLoginActive());
	Auth->CancelLogin(); Seed(Host);
	int32 Replies = 0;
	const auto CanceledLogout = Logout(Auth.ToSharedRef(), [&](bool) { ++Replies; });
	if (!CanceledLogout) return false;
	auto LateLogout = CanceledLogout->OnProcessRequestComplete();
	Auth->CancelLogout();
	LateLogout.ExecuteIfBound(CanceledLogout, MakeShared<FResponse, ESPMode::ThreadSafe>(204, FString()), true);
	TestEqual(TEXT("Cancellation discards logout completion"), Replies, 0);
	TestTrue(TEXT("Cancellation preserves credential"), Auth->HasAccessToken());
	const auto Last = Auth->CreateLoginRequest(TEXT("fixture"));
	if (!Last) return false;
	auto AfterDestroy = Last->OnProcessRequestComplete();
	const TWeakPtr<FRPGBackendAuthentication> Weak = Auth;
	Auth.Reset();
	TestFalse(TEXT("No request/delegate cycle retains service"), Weak.IsValid());
	TestFalse(TEXT("Destruction unbinds pending request"), Last->OnProcessRequestComplete().IsBound());
	AfterDestroy.ExecuteIfBound(Last, MakeShared<FResponse, ESPMode::ThreadSafe>(200, LoginBody()), true);
	TestEqual(TEXT("Dead service callback cannot notify"), Host.LoginReplies, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuthServiceReentryTest, "ProjectRPG.Online.AuthenticationService.LogoutReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGAuthServiceReentryTest::RunTest(const FString&)
{
	using namespace RPGAuthenticationServiceTests;
	FHost Host; Seed(Host);
	const auto Auth = MakeShared<FRPGBackendAuthentication>(Host, Host.State);
	bool bGuarded = false; FHttpRequestPtr Login;
	const auto Request = Auth->CreateLogoutRequest([&]
	{
		bGuarded = Auth->IsLogoutActive() && !Auth->CreateLoginRequest(TEXT("fixture")).IsValid();
		return true;
	}, [&](bool bSuccess) { if (bSuccess) Login = Auth->CreateLoginRequest(TEXT("fixture")); });
	if (!Request) return false;
	Complete(Request, 204);
	TestTrue(TEXT("Context validator runs with logout still pinned"), bGuarded);
	TestTrue(TEXT("Completion runs after slot release and can start explicit login"), Login.IsValid());
	Complete(Request, 204);
	TestTrue(TEXT("Duplicate logout cannot clear the next login"), Auth->IsLoginActive());
	TestEqual(TEXT("Duplicate logout cannot invalidate twice"), Host.Invalidations, 1);
	return true;
}
#endif
