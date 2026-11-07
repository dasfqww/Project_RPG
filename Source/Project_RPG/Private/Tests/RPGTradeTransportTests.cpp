#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeCoordinator.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace RPGTradeTransportTests
{
const FGuid Character(1, 1, 1, 1), Other(2, 2, 2, 2);
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FFake final : IRPGTradeTransport
{
    FString Endpoint = TEXT("http://localhost:3000/api"), Account = TEXT("76561198000000000");
    bool bToken = true, bSend = true;
    int32 Sends = 0, Cancels = 0;
    FString LastBody;
    TFunction<void(FRPGTradeHttpResult)> Completion;
    virtual bool GetContext(FString& E, FString& A) const override { E = Endpoint; A = Account; return !Account.IsEmpty(); }
    virtual bool CanSend() const override { return bToken; }
    virtual bool Send(FGuid, FGuid, const FString&, const FString& Body, TFunction<void(FRPGTradeHttpResult)> C) override
    { ++Sends; LastBody = Body; if (bSend) Completion = MoveTemp(C); return bSend; }
    virtual void Cancel() override { ++Cancels; Completion = {}; }
    void Reply(int32 Code, const FString& Body = TEXT(""), double Retry = 0)
    { auto C = MoveTemp(Completion); if (C) C({true, Code != 0, Code, Body, Retry}); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAuthRetryTest, "ProjectRPG.Trading.Transport.AuthRetry", RPGTradeTransportTests::Flags)
bool FRPGTradeAuthRetryTest::RunTest(const FString&)
{
    using namespace RPGTradeTransportTests;
    double Time = 100;
    auto Fake = MakeShared<FFake>(); auto C = MakeShared<FRPGTradeCoordinator>(Fake, [&] { return Time; });
    TestTrue(TEXT("select"), C->SelectCharacter(Character));
    TestTrue(TEXT("invite"), C->Invite(Other)); const auto Original = *C->GetClient().GetPending();
    Fake->bToken = false; Fake->Reply(401, TEXT("{\"error\":\"unauthorized\"}"));
    Time = 200; C->Pump();
    TestEqual(TEXT("waits for player token"), Fake->Sends, 1);
    TestNotNull(TEXT("expired auth retains pending"), C->GetClient().GetPending());
    TestTrue(TEXT("same character reselect retains pending"), C->SelectCharacter(Character));
    Fake->bToken = true; C->Pump();
    TestEqual(TEXT("retry after token renewal"), Fake->Sends, 2);
    TestEqual(TEXT("identical immutable retry body"), Fake->LastBody, Original.Body);
    TestTrue(TEXT("same request ID"), C->GetClient().GetPending()->RequestId == Original.RequestId);
    TestFalse(TEXT("no parallel replacement invite"), C->Invite(Other));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeRateTest, "ProjectRPG.Trading.Transport.RetryBounds", RPGTradeTransportTests::Flags)
bool FRPGTradeRateTest::RunTest(const FString&)
{
    using namespace RPGTradeTransportTests;
    double Time = 100;
    auto Fake = MakeShared<FFake>(); auto C = MakeShared<FRPGTradeCoordinator>(Fake, [&] { return Time; });
    C->SelectCharacter(Character); C->Invite(Other);
    Fake->Reply(429, TEXT("{\"error\":\"trade_rate_limit\"}"), 60);
    Time = 159.99; C->Pump(); TestFalse(TEXT("manual retry cannot bypass Retry-After"), C->Retry());
    TestEqual(TEXT("not sent early"), Fake->Sends, 1);
    Time = 160; C->Pump(); TestEqual(TEXT("sent after server deadline"), Fake->Sends, 2);
    for (int32 I = 0; I < 5; ++I) { Fake->Reply(503); Time += 100; C->Pump(); }
    TestEqual(TEXT("initial plus five automatic attempts"), Fake->Sends, 6);
    const auto Body = Fake->LastBody;
    Time += 1000; C->Pump(); TestEqual(TEXT("retry exhaustion pauses"), Fake->Sends, 6);
    TestTrue(TEXT("explicit retry resumes original intent"), C->Retry());
    TestEqual(TEXT("retry does not create a new intent"), Fake->LastBody, Body);
    Fake->Reply(0); Time = 1; C->Pump();
    TestEqual(TEXT("clock rollback cannot shorten deadline"), Fake->Sends, 7);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeContextTest, "ProjectRPG.Trading.Transport.ContextLifetime", RPGTradeTransportTests::Flags)
bool FRPGTradeContextTest::RunTest(const FString&)
{
    using namespace RPGTradeTransportTests;
    auto Fake = MakeShared<FFake>(); auto C = MakeShared<FRPGTradeCoordinator>(Fake);
    C->SelectCharacter(Character); C->Invite(Other);
    auto Late = Fake->Completion;
    Fake->Account = TEXT("76561198000000001"); C->Pump();
    TestNull(TEXT("account change clears old intent"), C->GetClient().GetPending());
    Late({true, true, 200, TEXT("{}"), 0}); TestFalse(TEXT("late callback cannot resurrect busy state"), C->IsBusy());
    TestFalse(TEXT("must explicitly select character in new account"), C->Invite(Other));
    C->SelectCharacter(Character); C->Invite(Other); Late = Fake->Completion;
    C->SelectCharacter(Other); Late({true, false, 0, TEXT(""), 0});
    TestNull(TEXT("old character callback ignored"), C->GetClient().GetPending());
    int32 Notifications = 0;
    C->OnChanged = [&] { ++Notifications; C->Reset(); };
    TestTrue(TEXT("reentrant select callback"), C->SelectCharacter(Character));
    TestEqual(TEXT("one notification"), Notifications, 1);
    TestFalse(TEXT("reset in notification retained"), C->Invite(Other));
    C->OnChanged = {};
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeReadTest, "ProjectRPG.Trading.Transport.ReadAndFailure", RPGTradeTransportTests::Flags)
bool FRPGTradeReadTest::RunTest(const FString&)
{
    using namespace RPGTradeTransportTests;
    double Time = 100;
    auto Fake = MakeShared<FFake>(); auto C = MakeShared<FRPGTradeCoordinator>(Fake, [&] { return Time; });
    C->SelectCharacter(Character); C->Invite(Other); Fake->Reply(0); const auto Original = Fake->LastBody;
    TestTrue(TEXT("can query after uncertain write"), C->Refresh());
    Fake->Reply(200, TEXT("{\"session\":null}"));
    TestTrue(TEXT("null query is observed"), C->GetLastResponse() == ERPGTradeResponse::NoActiveTrade);
    TestNotNull(TEXT("null query does not clear intent"), C->GetClient().GetPending());
    Time = 101; C->Pump(); Fake->Reply(409, TEXT("{\"error\":\"trade_revision_conflict\"}"));
    TestNotNull(TEXT("nonreceipt conflict after uncertainty retains intent"), C->GetClient().GetPending());
    TestEqual(TEXT("body unchanged by reads"), Fake->LastBody, Original);
    C->Reset(); C->SelectCharacter(Character); Fake->bSend = false;
    TestTrue(TEXT("intent retained when dispatch cannot start"), C->Invite(Other));
    TestFalse(TEXT("failed dispatch not permanently busy"), C->IsBusy());
    TestTrue(TEXT("dispatch failure enters uncertain state"), C->GetClient().GetPending()->Delivery == ERPGTradeDelivery::Unknown);
    return true;
}

struct FRPGTradeHttpRun
{
    FAutomationTestBase* Test = nullptr;
    TSharedPtr<FRPGTradeCoordinator> Client;
    TSharedPtr<FRPGHttpTradeTransport> Transport, Unauthorized;
    FString Endpoint;
    double Started = 0;
    int32 Stage = 0;
    bool bUnauthorizedDone = false;
};
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRPGTradeHttpWait, TSharedPtr<FRPGTradeHttpRun>, Run);
bool FRPGTradeHttpWait::Update()
{
    using namespace RPGTradeTransportTests;
    if (FPlatformTime::Seconds() - Run->Started > 45)
    { Run->Test->AddError(TEXT("Owned loopback trading HTTP test timed out")); Run->Client->Reset(); return true; }
    Run->Client->Pump();
    if (Run->Client->IsBusy()) return false;
    if (Run->Stage == 0)
    {
        Run->Test->TestTrue(TEXT("real authenticated GET active null"), Run->Client->GetLastResponse() == ERPGTradeResponse::NoActiveTrade);
        Run->Test->TestTrue(TEXT("real invite dispatch"), Run->Client->Invite(Other));
        Run->Stage = 1; return false;
    }
    if (Run->Stage == 1)
    {
        if (Run->Client->GetClient().GetPending()) return false;
        const auto* Session = Run->Client->GetClient().GetSession();
        if (!Run->Test->TestNotNull(TEXT("receipt after lost response/replay"), Session)) return true;
        Run->Test->TestTrue(TEXT("invite receipt state"), Session->Status == ERPGTradeStatus::Invited);
        Run->Test->TestTrue(TEXT("owner-scoped GET by trade"), Run->Client->Refresh(Session->TradeId));
        Run->Stage = 2; return false;
    }
    if (Run->Stage == 2)
    {
        Run->Test->TestTrue(TEXT("real GET specific state"), Run->Client->GetLastResponse() == ERPGTradeResponse::Applied);
        auto Context = [Endpoint = Run->Endpoint](FRPGTradeHttpContext& Out)
        { Out = {Endpoint, TEXT("76561198000000000"), TEXT("Bearer invalid-test-player")}; return true; };
        Run->Unauthorized = MakeShared<FRPGHttpTradeTransport>(Context, [](const FRPGTradeHttpContext&, int32) { return true; });
        Run->Test->TestFalse(TEXT("arbitrary action rejected before HTTP"), Run->Unauthorized->Send(Character, FGuid::NewGuid(), TEXT("../../admin"), TEXT("{}"), [](FRPGTradeHttpResult) {}));
        TWeakPtr<FRPGTradeHttpRun> WeakRun = Run;
        Run->Unauthorized->Send(Character, {}, TEXT(""), TEXT(""), [WeakRun](FRPGTradeHttpResult R)
        {
            if (auto State = WeakRun.Pin())
            {
                State->Test->TestTrue(TEXT("actual missing player credential yields 401"), R.bTransportSuccessful && R.bCurrentPlayerContext && R.StatusCode == 401);
                State->bUnauthorizedDone = true;
            }
        });
        Run->Stage = 3; return false;
    }
    return Run->bUnauthorizedDone;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeHttpTest, "ProjectRPG.Trading.Transport.RealHttp", RPGTradeTransportTests::Flags)
bool FRPGTradeHttpTest::RunTest(const FString&)
{
    using namespace RPGTradeTransportTests;
    AddExpectedMessagePlain(TEXT("request failed, libcurl error: 52 (Server returned nothing (no headers, no data))"),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    AddExpectedMessagePlain(TEXT("libcurl info message cache "), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 0);
    AddExpectedMessage(TEXT("^[0-9A-Fa-f]+ POST http://127\\.0\\.0\\.1:[0-9]+/api/characters/[0-9a-f-]+/trades/[0-9a-f-]+/invite completed with reason 'Other' after "),
        ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    auto Run = MakeShared<FRPGTradeHttpRun>(); Run->Test = this; Run->Started = FPlatformTime::Seconds();
    if (!FParse::Value(FCommandLine::Get(), TEXT("TradeTestEndpoint="), Run->Endpoint)) { AddError(TEXT("Fixture endpoint required")); return false; }
    Run->Transport = MakeShared<FRPGHttpTradeTransport>([Endpoint = Run->Endpoint](FRPGTradeHttpContext& Out)
        { Out = {Endpoint, TEXT("76561198000000000"), TEXT("Bearer trade-test-player")}; return true; },
        [](const FRPGTradeHttpContext&, int32) { return true; });
    Run->Client = MakeShared<FRPGTradeCoordinator>(Run->Transport.ToSharedRef());
    Run->Client->SelectCharacter(Character); Run->Client->Refresh();
    ADD_LATENT_AUTOMATION_COMMAND(FRPGTradeHttpWait(Run));
    return true;
}
#endif
