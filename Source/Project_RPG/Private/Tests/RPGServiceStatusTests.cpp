#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGServiceStatusSubsystem.h"
#include "Manager/HttpWebManager.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"

struct FRPGServiceStatusTestAccess
{
	static void Cache(URPGServiceStatusSubsystem* S, const FString& Api, const FRPGServiceStatusSnapshot& Value)
	{ S->Cancel(); S->StatusUrl = Api / TEXT("service-status"); S->State.Accept(Value, FPlatformTime::Seconds()); }
	static void Credential(UHttpWebManager* Web, const FString& Api)
	{
		Web->ApiUrl = Api; const auto Now = FDateTime::UtcNow(); const double Tick = FPlatformTime::Seconds();
		Web->PlayerAuth.AcceptLogin(TEXT("service-fixture-player-token"), TEXT("76561198012345678"),
			(Now + FTimespan::FromHours(1)).ToIso8601(), Now.ToIso8601(), Tick, Tick);
	}
	static void Backend(UHttpWebManager* Web, const FString& Api) { Web->ApiUrl = Api; }
	static void Attach(URPGOnlineLobbyWidget* Widget, URPGServiceStatusSubsystem* S, UHttpWebManager* Web)
	{
		Widget->ServiceStatus = S; Widget->Web = Web;
		Widget->WidgetTree = NewObject<UWidgetTree>(Widget);
		auto* Rows = Widget->WidgetTree->ConstructWidget<UVerticalBox>(); Widget->WidgetTree->RootWidget = Rows;
		Widget->BuildServiceStatusContent(*Rows); Widget->RefreshServicePresentation();
	}
	static float NoticeHeight(URPGOnlineLobbyWidget* Widget) { return CastChecked<USizeBox>(Widget->ServiceStatusContainer)->GetMaxDesiredHeight(); }
	static FText Text(URPGOnlineLobbyWidget* Widget) { Widget->RefreshServicePresentation(); return Widget->ServiceStatusText->GetText(); }
	static bool Blocked(URPGOnlineLobbyWidget* Widget, FName Name) { return Widget->IsServiceAdmissionBlocked(Name); }
	static void Detach(URPGOnlineLobbyWidget* Widget) { Widget->UnbindServiceStatus(); Widget->Web = nullptr; }
	static uint64 Generation(URPGServiceStatusSubsystem* S) { return S->Generation; }
	static void Pending(URPGServiceStatusSubsystem* S, FHttpRequestPtr Request) { S->Active = Request; }
	static void Complete(URPGServiceStatusSubsystem* S, FHttpRequestPtr Request, uint64 Generation, const FHttpResponsePtr& Response)
	{ S->Complete(Request, Response, true, Generation); }
	static void Login(UHttpWebManager* Web, const FString& Api)
	{
		auto Request = FHttpModule::Get().CreateRequest(); Request->SetURL(Api / TEXT("auth/steam-ticket")); Request->SetVerb(TEXT("POST"));
		Web->PlayerAuthentication->ActiveLogin = Request; Web->PlayerAuthentication->LoginStartedSeconds = FPlatformTime::Seconds();
		Request->OnProcessRequestComplete().BindUObject(Web, &UHttpWebManager::OnAuthenticateResponseReceived);
		Request->SetTimeout(8.f); Request->ProcessRequest();
	}
	static bool LoggingIn(UHttpWebManager* Web) { return Web->PlayerAuthentication->ActiveLogin.IsValid(); }
};

namespace
{
	const FString Body = TEXT("{\"contractVersion\":1,\"data\":{\"mode\":\"Open\",\"noticeText\":\"plain <script> notice\",\"noticeStartsAtUtc\":null,\"noticeEndsAtUtc\":null,\"noticeVisible\":true,\"observedAtUtc\":\"2026-11-06T00:00:00Z\"}}");
	class FServiceTestResponse final : public IHttpResponse
	{
		TArray<uint8> Bytes;
		FString Url = TEXT("http://service-fixture.invalid/api/service-status");
	public:
		FServiceTestResponse() { const FTCHARToUTF8 Utf8(*Body); Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()); }
		virtual const FString& GetURL() const override { return Url; }
		virtual const FString& GetEffectiveURL() const override { return Url; }
		virtual EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		virtual EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
		virtual FString GetURLParameter(const FString&) const override { return {}; }
		virtual FString GetHeader(const FString&) const override { return {}; }
		virtual TArray<FString> GetAllHeaders() const override { return {}; }
		virtual FString GetContentType() const override { return TEXT("application/json"); }
		virtual uint64 GetContentLength() const override { return Bytes.Num(); }
		virtual const TArray<uint8>& GetContent() const override { return Bytes; }
		virtual TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		virtual int32 GetResponseCode() const override { return 200; }
		virtual FString GetContentAsString() const override { return Body; }
		virtual FUtf8StringView GetContentAsUtf8StringView() const override { return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGServiceCodecTest, "ProjectRPG.Operations.ServiceStatus.Codec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGServiceCodecTest::RunTest(const FString&)
{
	FRPGServiceStatusSnapshot Value;
	TestTrue(TEXT("Public envelope decodes"), FRPGServiceStatusSnapshot::Decode(Body, Value));
	TestFalse(TEXT("Unsupported version rejected"), FRPGServiceStatusSnapshot::Decode(Body.Replace(TEXT("Version\":1"), TEXT("Version\":2")), Value));
	TestFalse(TEXT("Unknown mode rejected"), FRPGServiceStatusSnapshot::Decode(Body.Replace(TEXT("Open"), TEXT("Closed")), Value));
	TestFalse(TEXT("Contradictory visibility rejected"), FRPGServiceStatusSnapshot::Decode(Body.Replace(TEXT("Visible\":true"), TEXT("Visible\":false")), Value));
	TestFalse(TEXT("Oversized envelope rejected"), FRPGServiceStatusSnapshot::Decode(FString::ChrN(16385, TEXT('x')), Value));
	TestTrue(TEXT("Only exact 503 draining code recognized"), FRPGServiceStatusSnapshot::IsDrainResponse(503, TEXT("{\"error\":\"service_draining\"}")));
	TestFalse(TEXT("Unrelated outage is not a maintenance declaration"), FRPGServiceStatusSnapshot::IsDrainResponse(503, TEXT("{\"error\":\"unavailable\"}")));
	TestFalse(TEXT("403 cannot masquerade as draining"), FRPGServiceStatusSnapshot::IsDrainResponse(403, TEXT("{\"error\":\"service_draining\"}")));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGServiceFreshnessTest, "ProjectRPG.Operations.ServiceStatus.Freshness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGServiceFreshnessTest::RunTest(const FString&)
{
	FRPGServiceStatusSnapshot Value; FRPGServiceStatusSnapshot::Decode(Body, Value);
	Value.NoticeStartsAt = Value.ObservedAt + FTimespan::FromSeconds(10); Value.NoticeEndsAt = Value.ObservedAt + FTimespan::FromSeconds(20);
	FRPGServiceStatusState State; State.Accept(Value, 1000);
	TestTrue(TEXT("Future notice hidden"), State.Text(1000).IsEmpty());
	TestTrue(TEXT("Server-relative start inclusive"), State.Text(1010).ToString().Contains(TEXT("plain <script> notice")));
	TestTrue(TEXT("Server-relative end exclusive"), State.Text(1020).IsEmpty());
	TestTrue(TEXT("Old observation is explicitly stale"), State.IsStale(1046));
	TestFalse(TEXT("Stale notice is not shown as current"), State.Text(1046).ToString().Contains(TEXT("plain <script> notice")));
	State.Snapshot.Mode = TEXT("Drain"); State.bReadFailed = true;
	TestTrue(TEXT("Read outage cannot erase a known Drain"), State.IsDraining());
	TestTrue(TEXT("Login/new entry guarded"), FRPGServiceStatusState::IsNewAdmissionAction(TEXT("LoginSteamButton")) && FRPGServiceStatusState::IsNewAdmissionAction(TEXT("EnterDungeonButton")));
	for (FName Name : {FName(TEXT("RefreshDungeonSessionButton")), FName(TEXT("CancelDungeonDepartureButton")), FName(TEXT("RetryDungeonDepartureButton")), FName(TEXT("LogoutBackendButton"))})
		TestFalse(TEXT("Existing read/cancel/reconciliation paths preserved"), FRPGServiceStatusState::IsNewAdmissionAction(Name));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGServiceScopeTest, "ProjectRPG.Operations.ServiceStatus.ScopeAndWidget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGServiceScopeTest::RunTest(const FString&)
{
	auto* GI = NewObject<UGameInstance>(); auto* Service = NewObject<URPGServiceStatusSubsystem>(GI); auto* Web = NewObject<UHttpWebManager>(GI);
	auto* Widget = NewObject<URPGOnlineLobbyWidget>(GI); const FString Api = TEXT("http://service-fixture.invalid/api");
	FRPGServiceStatusSnapshot Value; FRPGServiceStatusSnapshot::Decode(Body, Value); Value.Mode = TEXT("Drain");
	FRPGServiceStatusTestAccess::Credential(Web, Api); FRPGServiceStatusTestAccess::Cache(Service, Api, Value);
	FRPGServiceStatusTestAccess::Attach(Widget, Service, Web);
	TestTrue(TEXT("Notice remains literal text in native UTextBlock"), FRPGServiceStatusTestAccess::Text(Widget).ToString().Contains(TEXT("plain <script> notice")));
	TestEqual(TEXT("Long notice cannot consume the whole login page"), FRPGServiceStatusTestAccess::NoticeHeight(Widget), 180.f);
	TestTrue(TEXT("Widget gates new entry"), FRPGServiceStatusTestAccess::Blocked(Widget, TEXT("EnterDungeonButton")));
	TestFalse(TEXT("Widget preserves cancellation"), FRPGServiceStatusTestAccess::Blocked(Widget, TEXT("CancelDungeonDepartureButton")));
	auto Old = FHttpModule::Get().CreateRequest(); Old->SetURL(Api / TEXT("service-status"));
	FRPGServiceStatusTestAccess::Pending(Service, Old); const uint64 Generation = FRPGServiceStatusTestAccess::Generation(Service);
	Service->ObserveDraining(Api); FRPGServiceStatusTestAccess::Complete(Service, Old, Generation, MakeShared<FServiceTestResponse, ESPMode::ThreadSafe>());
	TestTrue(TEXT("Earlier callback cannot undo authoritative refusal"), Service->GetState().bRefused && !Service->IsRequestInFlight());
	FRPGServiceStatusTestAccess::Backend(Web, TEXT("http://other-fixture.invalid/api"));
	TestFalse(TEXT("Old backend Drain cannot block a new profile"), FRPGServiceStatusTestAccess::Blocked(Widget, TEXT("EnterDungeonButton")));
	TestTrue(TEXT("Old backend notice is hidden immediately"), FRPGServiceStatusTestAccess::Text(Widget).IsEmpty());
	TestTrue(TEXT("Service observation never clears a player credential"), Web->HasAccessToken());
	Web->OnBackendServiceDraining.AddLambda([](const FString&) {});
	FRPGServiceStatusTestAccess::Detach(Widget); Service->Deinitialize(); Web->Deinitialize();
	TestFalse(TEXT("Manager shutdown releases service observers"), Web->OnBackendServiceDraining.IsBound());
	AddInfo(TEXT("ServiceStatusProducer.v2: bounded-scroll180, shutdown-observers, valid-old-Open fence.")); return true;
}

namespace
{
	struct FServiceIntegration : TSharedFromThis<FServiceIntegration>
	{
		FAutomationTestBase* Test; URPGServiceStatusSubsystem* Service; UHttpWebManager* Web; UGameInstance* GI;
		FString Origin, Key; FHttpRequestPtr Control; bool Pending = false, ControlOk = false; int32 Phase = 0, Refusals = 0; double Deadline;
		void Send(const FString& Command)
		{
			Pending = true; ControlOk = false; Control = FHttpModule::Get().CreateRequest();
			Control->SetURL(Origin / TEXT("verify") / Command); Control->SetVerb(TEXT("POST")); Control->SetHeader(TEXT("X-Verification-Key"), Key); Control->SetTimeout(8.f);
			Control->OnProcessRequestComplete().BindLambda([Self = AsShared()](FHttpRequestPtr, FHttpResponsePtr Response, bool Ok)
			{ Self->ControlOk = Ok && Response && Response->GetResponseCode() == 200; Self->Pending = false; });
			if (!Control->ProcessRequest()) Pending = false;
		}
		void Cleanup()
		{
			if (Control) { Control->OnProcessRequestComplete().Unbind(); Control->CancelRequest(); }
			Web->OnBackendServiceDraining.Clear(); Web->Deinitialize(); Service->Deinitialize(); Service->RemoveFromRoot(); Web->RemoveFromRoot(); GI->RemoveFromRoot();
		}
		bool Tick()
		{
			if (FPlatformTime::Seconds() > Deadline) { Test->AddError(TEXT("Bounded service HTTP integration deadline")); Cleanup(); return true; }
			if (Pending || Service->IsRequestInFlight()) return false;
			switch (Phase)
			{
			case 0:
				if (!Service->GetState().bKnown) { Test->AddError(TEXT("Initial real public HTTP read failed")); Cleanup(); return true; }
				Test->TestEqual(TEXT("Initial real HTTP Open"), Service->GetState().Snapshot.Mode, FString(TEXT("Open"))); Phase = 1; Send(TEXT("drain")); return false;
			case 1: if (!Test->TestTrue(TEXT("Backend service transition"), ControlOk)) { Cleanup(); return true; }
				Phase = 2; Service->Refresh(Origin / TEXT("api"), true); return false;
			case 2:
				Test->TestTrue(TEXT("Real public Drain parsed"), Service->GetState().IsDraining());
				Phase = 3; FRPGServiceStatusTestAccess::Login(Web, Origin / TEXT("api")); return false;
			case 3:
				if (FRPGServiceStatusTestAccess::LoggingIn(Web)) return false;
				Test->TestTrue(TEXT("Real authentication 503 reaches service hint without destroying auth"), Refusals == 1 && Service->GetState().bRefused && Web->HasAccessToken());
				Phase = 4; Send(TEXT("error")); return false;
			case 4: if (!Test->TestTrue(TEXT("Fixture read failure switch"), ControlOk)) { Cleanup(); return true; }
				Phase = 5; Service->Refresh(Origin / TEXT("api"), true); return false;
			case 5:
				Test->TestTrue(TEXT("Actual 500 stays Unknown and preserves prior Drain/auth"), Service->GetState().bReadFailed && Service->GetState().IsDraining() && Web->HasAccessToken());
				Phase = 6; Send(TEXT("open")); return false;
			case 6: if (!Test->TestTrue(TEXT("Backend returns to Open"), ControlOk)) { Cleanup(); return true; }
				Phase = 7; Service->Refresh(Origin / TEXT("api"), true); return false;
			case 7:
				Test->TestTrue(TEXT("Fresh real Open clears only service refusal, retains auth"), !Service->GetState().IsDraining() && !Service->GetState().bReadFailed && Web->HasAccessToken());
				Phase = 8; Send(TEXT("result")); return false;
			case 8: Test->TestTrue(TEXT("Backend verified credential-free status HTTP sequence"), ControlOk); Cleanup(); return true;
			default: Cleanup(); return true;
			}
		}
	};
	class FServiceIntegrationCommand final : public IAutomationLatentCommand
	{
		TSharedRef<FServiceIntegration> State;
	public: explicit FServiceIntegrationCommand(TSharedRef<FServiceIntegration> Value) : State(Value) {}
		virtual bool Update() override { return State->Tick(); }
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGServiceHttpTest, "ProjectRPG.Operations.ServiceStatus.HttpIntegration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGServiceHttpTest::RunTest(const FString&)
{
	FString Path, Json; TSharedPtr<FJsonObject> Bootstrap;
	if (!FParse::Value(FCommandLine::Get(), TEXT("RPGServiceStatusBootstrap="), Path) || !FFileHelper::LoadFileToString(Json, *Path)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Bootstrap) || !Bootstrap.IsValid()) return TestTrue(TEXT("Private service bootstrap required"), false);
	auto State = MakeShared<FServiceIntegration>(); State->Test = this; State->Origin = Bootstrap->GetStringField(TEXT("origin")); State->Key = Bootstrap->GetStringField(TEXT("key"));
	State->GI = NewObject<UGameInstance>(); State->GI->AddToRoot(); State->Service = NewObject<URPGServiceStatusSubsystem>(State->GI); State->Service->AddToRoot();
	State->Web = NewObject<UHttpWebManager>(State->GI); State->Web->AddToRoot(); FRPGServiceStatusTestAccess::Credential(State->Web, State->Origin / TEXT("api"));
	State->Web->OnBackendServiceDraining.AddLambda([State](const FString& Api) { ++State->Refusals; State->Service->ObserveDraining(Api); });
	State->Deadline = FPlatformTime::Seconds() + 90; State->Service->Refresh(State->Origin / TEXT("api"), true);
	TestFalse(TEXT("Concurrent status refresh is coalesced into the active real request"), State->Service->Refresh(State->Origin / TEXT("api"), true));
	AddCommand(new FServiceIntegrationCommand(State)); return true;
}
#endif
