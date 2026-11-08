#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

struct FRPGTradeOfferWidgetAccess { static void Destroy(URPGTradeWidget& W) { W.NativeDestruct(); } };
namespace RPGTradeOfferTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid First(1,1,1,1),Second(2,2,2,2),Trade(3,3,3,3),Item(5,5,5,5);
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
TSharedPtr<FJsonObject> Parse(const FString& T){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(T),O);return O;}
FString Snapshot(int32 Revision=5)
{
    return FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"%d","offerRevision":"1","expiresAt":"2030-01-01T00:00:00Z","status":"Negotiating","completed":false,"initiatorOffer":{"currencyAmount":"1","items":[]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":false,"inviteeReady":false,"initiatorConfirmed":false,"inviteeConfirmed":false})"),*Id(Trade),*Id(First),*Id(Second),Revision);
}
FString Envelope(int32 Rev=5){return TEXT("{\"session\":")+Snapshot(Rev)+TEXT("}");}
FString Preview(){return TEXT("{\"session\":")+Snapshot()+TEXT(",\"observedAt\":\"2026-11-08T00:00:00Z\",\"isOpen\":true,\"allItemsAvailable\":true,\"initiatorItems\":[],\"inviteeItems\":[]}");}
FRPGTradeSession Session(){FRPGTradeClient C;C.Bind(TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Second);C.CompleteRead(C.BeginRead(),200,Envelope());return *C.GetSession();}
FRPGTradeBagSnapshot Bag()
{
    FRPGTradeBagSnapshot B;B.Endpoint=TEXT("http://localhost:3000/api");B.Account=TEXT("76561198000000000");B.Character=Second;B.Token=FGuid::NewGuid();B.bReady=true;B.ValidUntil=130;
    B.Items.Add({Item,9007199254740997LL,10,FText::FromString(TEXT("검 · 장비 1칸")),true});return B;
}
struct FFake final : IRPGTradeTransport
{
    FString Body,Action;int32 Sends=0;TFunction<void(FRPGTradeHttpResult)> Callback;
    virtual bool GetContext(FString& E,FString& A) const override {E=TEXT("http://localhost:3000/api");A=TEXT("76561198000000000");return true;}
    virtual bool CanSend() const override{return true;}
    virtual bool Send(FGuid,FGuid,const FString& A,const FString& B,TFunction<void(FRPGTradeHttpResult)> C) override{++Sends;Action=A;Body=B;Callback=MoveTemp(C);return true;}
    virtual bool SendPreview(FGuid,FGuid,TFunction<void(FRPGTradeHttpResult)> C) override{++Sends;Action=TEXT("preview");Callback=MoveTemp(C);return true;}
    virtual void Cancel() override{Callback={};}
    void Reply(int32 Code,const FString& B=TEXT("")){auto C=MoveTemp(Callback);if(C)C({true,Code!=0,Code,B,0});}
};
struct FSetup
{
    double Time=100;FRPGTradeBagSnapshot CurrentBag=Bag();
    FRPGTradeLobbyContext Frame{TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Second,true,true,true,true};
    TSharedRef<FFake> Fake=MakeShared<FFake>();TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FSetup(){Owner=FRPGTradeLobbyOwner::Create(Fake,[this]{return Frame;},[this]{return Time;});Owner->SetBagSource([this]{return CurrentBag;},[this]{CurrentBag.Token=FGuid::NewGuid();CurrentBag.bReady=true;CurrentBag.ValidUntil=Time+30;return true;});}
    void Load(){Owner->Tick();Fake->Reply(200,Envelope());Time=102;Owner->Tick();Fake->Reply(200,Preview());}
    ~FSetup(){Owner->Shutdown();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeDraftValuesTest,"ProjectRPG.Trading.Offer.ValuesAndBounds",RPGTradeOfferTests::Flags)
bool FRPGTradeDraftValuesTest::RunTest(const FString&)
{
    using namespace RPGTradeOfferTests;auto B=Bag();auto S=Session();FRPGTradeOfferDraft D;FRPGTradeOffer O;
    TestTrue(TEXT("begin validated snapshot"),D.Begin(S,Second,B));
    for(const auto* Bad:{TEXT(""),TEXT("01"),TEXT("-1"),TEXT("+1"),TEXT("1.0"),TEXT("1e3"),TEXT("9223372036854775808")})
    {TestFalse(TEXT("noncanonical money blocked"),D.SetAmount(Bad));TestFalse(TEXT("invalid input never falls back to a prior amount"),D.Build(S,Second,B,O));}
    TestTrue(TEXT("full int64 amount"),D.SetAmount(TEXT("9223372036854775807")));
    TestTrue(TEXT("quantity from current bag"),D.SetItem(Item,3,B));TestFalse(TEXT("quantity over stack"),D.SetItem(Item,11,B));
    TestTrue(TEXT("build"),D.Build(S,Second,B,O));TestEqual(TEXT("amount never rounded"),O.CurrencyAmount,MAX_int64);TestEqual(TEXT("revision never rounded"),O.Items[0].ExpectedRevision,int64(9007199254740997LL));
    B.Items[0].bEligible=false;TestFalse(TEXT("locked/bound/unresolved bag entry blocked"),D.Build(S,Second,B,O));B.Items[0].bEligible=true;
    for(int32 I=0;I<8;++I){const FGuid New=FGuid::NewGuid();B.Items.Add({New,1,1,FText::FromString(TEXT("물품")),true});const bool Added=D.SetItem(New,1,B);TestEqual(TEXT("maximum eight distinct items"),Added,I<7);}
    auto Duplicate=B;Duplicate.Items.Add(B.Items[0]);TestFalse(TEXT("duplicate identity invalidates snapshot"),Duplicate.IsUsable(100));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeDraftScopeTest,"ProjectRPG.Trading.Offer.ScopeAndStaleness",RPGTradeOfferTests::Flags)
bool FRPGTradeDraftScopeTest::RunTest(const FString&)
{
    using namespace RPGTradeOfferTests;FSetup S;S.Load();TestTrue(TEXT("begin"),S.Owner->BeginDraft());const auto Token=S.Owner->GetDraftView().Token;
    TestTrue(TEXT("editing blocks consent to the old displayed server offer"),!S.Owner->GetView().bCanReview);
    S.CurrentBag.Account=TEXT("other");TestFalse(TEXT("foreign bag cannot stage items"),S.Owner->SetDraftItem(Token,Item,TEXT("1")));S.CurrentBag.Account=S.Frame.Account;
    S.CurrentBag.Token=FGuid::NewGuid();TestFalse(TEXT("reload never silently rebases an old draft"),S.Owner->SendDraft(Token));
    S.Owner->BeginDraft();const auto Fresh=S.Owner->GetDraftView().Token;S.Time=131;TestFalse(TEXT("expired read cannot send"),S.Owner->SendDraft(Fresh));S.Time=100;TestFalse(TEXT("clock rollback cannot revive a bag"),S.Owner->SendDraft(Fresh));
    S.Time=132;S.CurrentBag.ValidUntil=200;S.Owner->BeginDraft();const auto Current=S.Owner->GetDraftView().Token;
    S.Owner->Refresh();S.Fake->Reply(200,TEXT("{\"session\":")+Snapshot(6)+TEXT(",\"observedAt\":\"2026-11-08T00:00:00Z\",\"isOpen\":true,\"allItemsAvailable\":true,\"initiatorItems\":[],\"inviteeItems\":[]}"));
    TestFalse(TEXT("peer revision cannot be silently adopted"),S.Owner->SendDraft(Current));
    auto B=Bag();auto State=Session();State.InviteeOffer.Items.Add({Item,1,2});FRPGTradeOfferDraft D;FRPGTradeOffer O;D.Begin(State,Second,B);
    TestFalse(TEXT("stale existing offer cannot be sent"),D.Build(State,Second,B,O));TestFalse(TEXT("explicit remove is required before choosing a changed revision"),D.SetItem(Item,2,B));
    D.RemoveItem(Item);D.SetItem(Item,2,B);TestTrue(TEXT("explicit remove and re-add captures current revision"),D.Build(State,Second,B,O));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeDraftReplayTest,"ProjectRPG.Trading.Offer.ImmutableReplay",RPGTradeOfferTests::Flags)
bool FRPGTradeDraftReplayTest::RunTest(const FString&)
{
    using namespace RPGTradeOfferTests;FSetup S;S.Load();S.Owner->BeginDraft();const auto Token=S.Owner->GetDraftView().Token;
    S.Owner->SetDraftItem(Token,Item,TEXT("3"));S.Owner->SetDraftAmount(Token,TEXT("9007199254740993"));
    TestTrue(TEXT("send edited offer"),S.Owner->SendDraft(Token));const auto Original=*S.Owner->GetClient().GetPending();auto O=Parse(Original.Body);
    TestEqual(TEXT("exact session expectation"),O->GetStringField(TEXT("expectedRevision")),FString(TEXT("5")));
    TestEqual(TEXT("exact amount string"),O->GetObjectField(TEXT("offer"))->GetStringField(TEXT("currencyAmount")),FString(TEXT("9007199254740993")));
    S.Fake->Reply(0);S.CurrentBag.bReady=false;S.Time=200;S.Owner->Tick();
    TestEqual(TEXT("retry does not require a new bag or new draft"),S.Fake->Body,Original.Body);
    TestTrue(TEXT("same ID survives bag invalidation"),S.Owner->GetClient().GetPending()->RequestId==Original.RequestId);
    TestFalse(TEXT("cannot replace an uncertain proposal"),S.Owner->BeginDraft());TestFalse(TEXT("old draft token cannot be sent twice"),S.Owner->SendDraft(Token));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeDraftWidgetTest,"ProjectRPG.Trading.Offer.WidgetEditing",RPGTradeOfferTests::Flags)
bool FRPGTradeDraftWidgetTest::RunTest(const FString&)
{
    using namespace RPGTradeOfferTests;FSetup S;S.Load();auto* W=NewObject<URPGTradeWidget>();W->Initialize();W->SetTradeOwner(S.Owner.ToSharedRef());const auto Slate=W->TakeWidget();
    auto Click=[&](const TCHAR* Name){auto* B=Cast<UButton>(W->GetWidgetFromName(Name));if(TestNotNull(Name,B))B->OnClicked.Broadcast();};
    Click(TEXT("TradeReloadBag"));Click(TEXT("TradeBeginDraft"));
    auto* BagBox=Cast<UComboBoxString>(W->GetWidgetFromName(TEXT("TradeBagItems")));auto* Amount=Cast<UEditableTextBox>(W->GetWidgetFromName(TEXT("TradeDraftAmount")));auto* Qty=Cast<UEditableTextBox>(W->GetWidgetFromName(TEXT("TradeDraftQuantity")));
    if(!TestNotNull(TEXT("bag selector"),BagBox)||!TestNotNull(TEXT("amount input"),Amount)||!TestNotNull(TEXT("quantity input"),Qty))return false;
    BagBox->SetSelectedIndex(0);BagBox->OnSelectionChanged.Broadcast(BagBox->GetSelectedOption(),ESelectInfo::Direct);Qty->SetText(FText::FromString(TEXT("3")));Qty->OnTextChanged.Broadcast(Qty->GetText());
    TestTrue(TEXT("actual quantity input enables add"),Cast<UButton>(W->GetWidgetFromName(TEXT("TradeAddItem")))->GetIsEnabled());Click(TEXT("TradeAddItem"));
    Amount->SetText(FText::FromString(TEXT("-1")));Amount->OnTextChanged.Broadcast(Amount->GetText());TestFalse(TEXT("invalid amount disables real send control"),Cast<UButton>(W->GetWidgetFromName(TEXT("TradeSendOffer")))->GetIsEnabled());
    Amount->SetText(FText::FromString(TEXT("9007199254740993")));Amount->OnTextChanged.Broadcast(Amount->GetText());TestTrue(TEXT("valid real editor can send"),Cast<UButton>(W->GetWidgetFromName(TEXT("TradeSendOffer")))->GetIsEnabled());
    W->CloseScreen();FRPGTradeOfferWidgetAccess::Destroy(*W);
    auto* Reopened=NewObject<URPGTradeWidget>();Reopened->Initialize();Reopened->SetTradeOwner(S.Owner.ToSharedRef());const auto Slate2=Reopened->TakeWidget();
    TestEqual(TEXT("unsent amount retained after close"),Cast<UEditableTextBox>(Reopened->GetWidgetFromName(TEXT("TradeDraftAmount")))->GetText().ToString(),FString(TEXT("9007199254740993")));
    Cast<UButton>(Reopened->GetWidgetFromName(TEXT("TradeSendOffer")))->OnClicked.Broadcast();
    if(!TestNotNull(TEXT("actual editor dispatch"),S.Owner->GetClient().GetPending()))return false;
    TestEqual(TEXT("actual selected quantity"),Parse(S.Fake->Body)->GetObjectField(TEXT("offer"))->GetArrayField(TEXT("items"))[0]->AsObject()->GetIntegerField(TEXT("quantity")),3);
    TestFalse(TEXT("no internal item ID in user choice"),BagBox->GetOptionAtIndex(0).Contains(Id(Item)));FRPGTradeOfferWidgetAccess::Destroy(*Reopened);return true;
}

struct FRPGOfferHttpRun
{FAutomationTestBase* Test=nullptr;TSharedPtr<FRPGHttpTradeTransport> Transport;FRPGTradeClient Client;double Started=0,RetryAt=0;bool Busy=false,Done=false;FString Original;};
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRPGOfferHttpWait,TSharedPtr<FRPGOfferHttpRun>,Run);
bool FRPGOfferHttpWait::Update()
{
    using namespace RPGTradeOfferTests;const double Now=FPlatformTime::Seconds();
    if(Now-Run->Started>30){Run->Test->AddError(TEXT("Offer HTTP timeout"));Run->Transport->Cancel();return true;}if(Run->Done)return true;
    if(Run->Busy || Now<Run->RetryAt)return false;FRPGTradeTicket Ticket;if(!Run->Client.BeginAttempt(Ticket))return false;
    Run->Busy=true;const auto Request=*Run->Client.GetPending();Run->Test->TestEqual(TEXT("actual retry bytes"),Request.Body,Run->Original);TWeakPtr<FRPGOfferHttpRun> Weak=Run;
    Run->Transport->Send(Second,Trade,TEXT("offer"),Request.Body,[Weak,Ticket](FRPGTradeHttpResult R)
    {if(auto S=Weak.Pin()){S->Busy=false;const auto Result=S->Client.CompleteAttempt(Ticket,R.bCurrentPlayerContext && R.bTransportSuccessful?R.StatusCode:0,R.Body);
        if(Result==ERPGTradeResponse::Unknown)S->RetryAt=FPlatformTime::Seconds()+1;else {S->Test->TestTrue(TEXT("actual offer receipt"),Result==ERPGTradeResponse::Succeeded);S->Done=true;}}});return false;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeDraftHttpTest,"ProjectRPG.Trading.Offer.RealHttp",RPGTradeOfferTests::Flags)
bool FRPGTradeDraftHttpTest::RunTest(const FString&)
{
    using namespace RPGTradeOfferTests;FString Endpoint;if(!FParse::Value(FCommandLine::Get(),TEXT("TradeTestEndpoint="),Endpoint)){AddError(TEXT("Fixture endpoint required"));return false;}
    AddExpectedMessagePlain(TEXT("request failed, libcurl error: 52 (Server returned nothing (no headers, no data))"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
    AddExpectedMessagePlain(TEXT("libcurl info message cache "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,0);
    AddExpectedMessage(TEXT("^[0-9A-Fa-f]+ POST http://127\\.0\\.0\\.1:[0-9]+/api/characters/[0-9a-f-]+/trades/[0-9a-f-]+/offer completed with reason 'Other' after "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
    auto Run=MakeShared<FRPGOfferHttpRun>();Run->Test=this;Run->Started=FPlatformTime::Seconds();
    Run->Transport=MakeShared<FRPGHttpTradeTransport>([Endpoint](FRPGTradeHttpContext& O){O={Endpoint,TEXT("76561198000000000"),TEXT("Bearer trade-test-player")};return true;},[](const FRPGTradeHttpContext&,int32){return true;});
    Run->Client.Bind(Endpoint,TEXT("76561198000000000"),Second);Run->Client.CompleteRead(Run->Client.BeginRead(Trade),200,Envelope());
    auto B=Bag();FRPGTradeOfferDraft Draft;FRPGTradeOffer Offer;Draft.Begin(*Run->Client.GetSession(),Second,B);Draft.SetAmount(TEXT("9007199254740993"));Draft.SetItem(Item,3,B);Draft.Build(*Run->Client.GetSession(),Second,B,Offer);
    Run->Client.Prepare(ERPGTradeAction::Offer,&Offer);Run->Original=Run->Client.GetPending()->Body;ADD_LATENT_AUTOMATION_COMMAND(FRPGOfferHttpWait(Run));return true;
}
#endif
