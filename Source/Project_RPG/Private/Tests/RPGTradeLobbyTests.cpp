#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"

struct FRPGTradeWidgetTestAccess
{
    static URPGTradeWidget* Make(TSharedRef<FRPGTradeLobbyOwner> Owner)
    { auto* W = NewObject<URPGTradeWidget>(); W->SetTradeOwner(Owner); W->RebuildWidget(); return W; }
    static void Destroy(URPGTradeWidget& W) { W.NativeDestruct(); }
    static UButton* Accept(URPGTradeWidget& W) { return W.AcceptButton; }
    static UButton* Cancel(URPGTradeWidget& W) { return W.CancelButton; }
    static void Close(URPGTradeWidget& W) { W.CloseTrade(); }
    static FString Text(URPGTradeWidget& W) { return W.StatusText->GetText().ToString(); }
};
namespace RPGTradeLobbyTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid First(1,1,1,1), Second(2,2,2,2), Trade(3,3,3,3);
FString Id(FGuid Value) { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
FString Snapshot(const FString& Status = TEXT("Invited"), int32 Revision = 1)
{
    return FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"%d","offerRevision":"1","expiresAt":"2030-01-01T00:00:00Z","status":"%s","completed":false,"initiatorOffer":{"currencyAmount":"0","items":[]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":false,"inviteeReady":false,"initiatorConfirmed":false,"inviteeConfirmed":false})"), *Id(Trade),*Id(First),*Id(Second),Revision,*Status);
}
struct FFake final : IRPGTradeTransport
{
    FString Account = TEXT("76561198000000000"), Body, Action;
    FGuid Actor;
    int32 Sends = 0, Cancels = 0;
    TFunction<void(FRPGTradeHttpResult)> Completion;
    virtual bool GetContext(FString& E, FString& A) const override { E=TEXT("http://localhost:3000/api"); A=Account; return !Account.IsEmpty(); }
    virtual bool CanSend() const override { return true; }
    virtual bool Send(FGuid Character, FGuid, const FString& A, const FString& B, TFunction<void(FRPGTradeHttpResult)> C) override
    { ++Sends; Actor=Character; Body=B; Action=A; Completion=MoveTemp(C); return true; }
    virtual void Cancel() override { ++Cancels; Completion={}; }
    void Reply(int32 Code, const FString& B=TEXT("")) { auto C=MoveTemp(Completion); if(C)C({true,Code!=0,Code,B,0}); }
    void Session(const FString& S) { Reply(200,TEXT("{\"session\":")+S+TEXT("}")); }
};
struct FSetup
{
    FRPGTradeLobbyContext Context;
    double Time=100;
    TSharedRef<FFake> Fake=MakeShared<FFake>();
    TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FSetup()
    {
        Context={TEXT("http://localhost:3000/api"),Fake->Account,Second,true,true,true,true};
        Owner=FRPGTradeLobbyOwner::Create(Fake,[this]{return Context;},[this]{return Time;});
    }
    void Invited() { Owner->Tick(); Fake->Session(Snapshot()); }
    ~FSetup() { Owner->Shutdown(); }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLobbyLifetimeTest,"ProjectRPG.Trading.Lobby.ScreenLifetime",RPGTradeLobbyTests::Flags)
bool FRPGTradeLobbyLifetimeTest::RunTest(const FString&)
{
    using namespace RPGTradeLobbyTests;
    FSetup S; S.Invited();
    auto* FirstView=FRPGTradeWidgetTestAccess::Make(S.Owner.ToSharedRef());
    TestTrue(TEXT("actual accept control enabled for invitee"),FRPGTradeWidgetTestAccess::Accept(*FirstView)->GetIsEnabled());
    FRPGTradeWidgetTestAccess::Accept(*FirstView)->OnClicked.Broadcast();
    if(!TestNotNull(TEXT("actual button starts intent"),S.Owner->GetClient().GetPending()))return false;
    S.Fake->Reply(0); const auto Original=*S.Owner->GetClient().GetPending();
    FRPGTradeWidgetTestAccess::Close(*FirstView); FRPGTradeWidgetTestAccess::Destroy(*FirstView);
    TestTrue(TEXT("close retains same intent"),S.Owner->GetClient().GetPending()->RequestId==Original.RequestId);
    auto* SecondView=FRPGTradeWidgetTestAccess::Make(S.Owner.ToSharedRef());
    TestFalse(TEXT("reopened view cannot issue duplicate accept"),FRPGTradeWidgetTestAccess::Accept(*SecondView)->GetIsEnabled());
    TestFalse(TEXT("pending cannot be replaced by cancel"),S.Owner->Submit(ERPGTradeAction::Cancel));
    S.Time=102; S.Owner->Tick();
    TestEqual(TEXT("retry still byte identical after screen destruction"),S.Fake->Body,Original.Body);
    TestTrue(TEXT("reopened status uses owner"),!FRPGTradeWidgetTestAccess::Text(*SecondView).IsEmpty());
    FRPGTradeWidgetTestAccess::Destroy(*SecondView);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLobbyPauseTest,"ProjectRPG.Trading.Lobby.PauseAndRecovery",RPGTradeLobbyTests::Flags)
bool FRPGTradeLobbyPauseTest::RunTest(const FString&)
{
    using namespace RPGTradeLobbyTests;
    FSetup S; S.Invited(); S.Owner->Submit(ERPGTradeAction::Accept); S.Fake->Reply(0);
    const auto Original=*S.Owner->GetClient().GetPending();
    S.Context.bLocalLobby=false; S.Time=1000; S.Owner->Tick();
    TestEqual(TEXT("no replay outside lobby"),S.Fake->Sends,2);
    TestNotNull(TEXT("travel does not erase unknown result"),S.Owner->GetClient().GetPending());
    TestFalse(TEXT("cannot mutate outside lobby"),S.Owner->Submit(ERPGTradeAction::Cancel));
    S.Context.bLocalLobby=true; S.Context.bAuthenticated=false; S.Owner->Tick();
    TestEqual(TEXT("waits for authentication"),S.Fake->Sends,2);
    S.Context.bAuthenticated=true; S.Owner->Tick();
    TestEqual(TEXT("same intent resumes in lobby"),S.Fake->Body,Original.Body);
    TestEqual(TEXT("one retry after return"),S.Fake->Sends,3);
    TestTrue(TEXT("transition lock persists"),S.Owner->BlocksTransitions());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLobbyIdentityTest,"ProjectRPG.Trading.Lobby.Identity",RPGTradeLobbyTests::Flags)
bool FRPGTradeLobbyIdentityTest::RunTest(const FString&)
{
    using namespace RPGTradeLobbyTests;
    FSetup S; S.Invited();
    S.Context.Character=First; S.Time=102; S.Owner->Tick();
    TestEqual(TEXT("old actor pinned while trade is open"),S.Fake->Actor,Second);
    TestFalse(TEXT("new selected character cannot accept old invitation"),S.Owner->GetView().bAccept);
    S.Fake->Session(Snapshot());
    S.Context.Account=TEXT("76561198000000001"); S.Fake->Account=S.Context.Account; S.Owner->Tick();
    TestNull(TEXT("account boundary clears old session"),S.Owner->GetClient().GetSession());
    const int32 Sends=S.Fake->Sends; S.Owner->Tick();
    TestEqual(TEXT("new account does not inherit stale character selection"),S.Fake->Sends,Sends);
    S.Context.Character.Invalidate(); S.Owner->Tick(); S.Context.Character=Second; S.Owner->Tick();
    TestEqual(TEXT("fresh selection queries new account"),S.Fake->Sends,Sends+1);
    auto Callback=S.Fake->Completion; S.Owner->Shutdown(); Callback({true,true,200,TEXT("{\"session\":null}"),0});
    TestFalse(TEXT("late callback after shutdown does not revive owner"),S.Owner->IsAvailable());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLobbyPollingTest,"ProjectRPG.Trading.Lobby.PollingAndControls",RPGTradeLobbyTests::Flags)
bool FRPGTradeLobbyPollingTest::RunTest(const FString&)
{
    using namespace RPGTradeLobbyTests;
    FSetup S; S.Context.bEnabled=false; S.Owner->Tick();
    TestEqual(TEXT("default disabled sends no requests"),S.Fake->Sends,0);
    S.Context.bEnabled=true; S.Owner->Tick(); S.Fake->Session(TEXT("null"));
    S.Time=101.99; S.Owner->Tick(); TestEqual(TEXT("polling is bounded"),S.Fake->Sends,1);
    S.Time=102; S.Owner->Tick(); TestEqual(TEXT("polls active trade without an open widget"),S.Fake->Sends,2);
    S.Fake->Session(Snapshot());
    TestFalse(TEXT("asset ready not exposed by this owner"),S.Owner->Submit(ERPGTradeAction::Ready));
    TestFalse(TEXT("asset confirm not exposed by this owner"),S.Owner->Submit(ERPGTradeAction::Confirm));
    TestTrue(TEXT("live trade pins transition controls"),S.Owner->BlocksTransitions());
    S.Context.bEnabled=false; S.Owner->Tick();
    TestTrue(TEXT("feature retirement still exposes live recovery"),S.Owner->IsAvailable());
    TestFalse(TEXT("retired feature cannot accept new consent"),S.Owner->Submit(ERPGTradeAction::Accept));
    TestTrue(TEXT("retired feature permits cancel"),S.Owner->Submit(ERPGTradeAction::Cancel));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeLobbyWidgetTest,"ProjectRPG.Trading.Lobby.WidgetSafety",RPGTradeLobbyTests::Flags)
bool FRPGTradeLobbyWidgetTest::RunTest(const FString&)
{
    using namespace RPGTradeLobbyTests;
    FSetup S; S.Invited();
    auto* View=FRPGTradeWidgetTestAccess::Make(S.Owner.ToSharedRef());
    const auto* Ready = Cast<UButton>(View->GetWidgetFromName(TEXT("TradeReady")));
    const auto* Confirm = Cast<UButton>(View->GetWidgetFromName(TEXT("TradeConfirm")));
    TestTrue(TEXT("unverified ready remains disabled"),Ready && !Ready->GetIsEnabled());
    TestTrue(TEXT("unverified final-confirm remains disabled"),Confirm && !Confirm->GetIsEnabled());
    TestFalse(TEXT("status does not expose participant UUID"),FRPGTradeWidgetTestAccess::Text(*View).Contains(Id(First)));
    TestTrue(TEXT("cancel enabled"),FRPGTradeWidgetTestAccess::Cancel(*View)->GetIsEnabled());
    FRPGTradeWidgetTestAccess::Cancel(*View)->OnClicked.Broadcast();
    const auto* Pending=S.Owner->GetClient().GetPending();
    if(!TestNotNull(TEXT("real cancel click creates pending request"),Pending))return false;
    const auto Request=Pending->RequestId;
    const FString Receipt=TEXT("{\"requestId\":\"")+Id(Request)+TEXT("\",\"session\":")+Snapshot(TEXT("Cancelled"),2)+TEXT(",\"rejection\":null,\"committedAt\":\"2026-11-08T00:00:00Z\"}");
    S.Fake->Reply(200,Receipt);
    TestFalse(TEXT("terminal session releases transition lock"),S.Owner->BlocksTransitions());
    TestFalse(TEXT("closed trade cannot cancel again"),FRPGTradeWidgetTestAccess::Cancel(*View)->GetIsEnabled());
    int32 Closed=0; View->OnClosed.AddLambda([&]{++Closed;});
    FRPGTradeWidgetTestAccess::Close(*View);
    TestEqual(TEXT("explicit close notifies focus owner once"),Closed,1);
    View->OnClosed.Clear();
    TestNull(TEXT("explicit closed acknowledgement releases terminal display"),S.Owner->GetClient().GetSession());
    FRPGTradeWidgetTestAccess::Destroy(*View);
    return true;
}
#endif
