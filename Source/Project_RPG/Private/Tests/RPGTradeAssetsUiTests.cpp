#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "Progression/RPGEquipmentReadGuard.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace RPGTradeAssetsUiTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid Actor(1,1,1,1),Other(2,2,2,2),Trade(3,3,3,3);
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Session(const FString& Status=TEXT("Completed"))
{
    const bool Done=Status==TEXT("Completed");
    return FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"10","offerRevision":"2","expiresAt":"2030-01-01T00:00:00Z","status":"%s","completed":%s,"initiatorOffer":{"currencyAmount":"1","items":[]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":true,"inviteeReady":true,"initiatorConfirmed":%s,"inviteeConfirmed":%s})"),*Id(Trade),*Id(Actor),*Id(Other),*Status,Done?TEXT("true"):TEXT("false"),Done?TEXT("true"):TEXT("false"),Done?TEXT("true"):TEXT("false"));
}
FString Envelope(const FString& Status=TEXT("Completed")){return TEXT("{\"session\":")+Session(Status)+TEXT("}");}
FRPGTradeSession Completed(){FRPGTradeClient C;C.Bind(TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Actor);C.CompleteRead(C.BeginRead(),200,Envelope());return *C.GetSession();}
FString Item(int32 Index=5,bool Mail=false)
{
    return FString::Printf(TEXT(R"({"definitionType":"Material","definitionName":"Api.Ore","definitionVersion":1,"owner":{"type":"Character","ownerId":"%s"},"location":{"containerType":"%s","containerId":"%s","slotIndex":%d},"state":{"instanceId":"%s","generationSeed":1234,"quantity":3,"instanceTags":[],"statValues":[]},"revision":9007199254740997,"lifecycleState":"Active","metadata":{"bindState":"Unbound","durability":{"current":0,"maximum":0},"expiresAtUtc":null,"creationSource":"PrivateSource","isLocked":false}})"),*Id(Actor),Mail?TEXT("Mail"):TEXT("Inventory"),*Id(Actor),Mail?Index:160,*Id(FGuid(Index,Index,Index,Index)));
}
FString Payload(const FString& Rows=Item()+TEXT(",")+Item(6,true))
{
    return TEXT("{\"session\":")+Session()+FString::Printf(TEXT(R"(,"characterId":"%s","observedAt":"2026-11-09T00:00:00Z","isComplete":true,"wallet":{"characterId":"%s","rosterId":"00000004-0000-0004-0000-000400000004","accountId":"76561198000000000","balances":[{"currencyCode":"Gold","displayName":"Gold","scope":"Account","ownerId":"76561198000000000","balance":9007199254740993,"maxBalance":9223372036854775807,"revision":9007199254740995}]},"items":[%s]})"),*Id(Actor),*Id(Actor),*Rows);
}
struct FFake final:IRPGTradeTransport
{
    FString Account=TEXT("76561198000000000"),Body;int32 Gets=0;TFunction<void(FRPGTradeHttpResult)> Callback;
    virtual bool GetContext(FString& E,FString& A)const override{E=TEXT("http://localhost:3000/api");A=Account;return true;}
    virtual bool CanSend()const override{return true;}
    virtual bool Send(FGuid,FGuid,const FString&,const FString& B,TFunction<void(FRPGTradeHttpResult)> C)override{Body=B;Callback=MoveTemp(C);return true;}
    virtual bool SendPreview(FGuid,FGuid,TFunction<void(FRPGTradeHttpResult)> C)override{Callback=MoveTemp(C);return true;}
    virtual bool SendAssets(FGuid,FGuid,TFunction<void(FRPGTradeHttpResult)> C)override{++Gets;Callback=MoveTemp(C);return true;}
    virtual void Cancel()override{Callback={};}
    void Reply(int32 Code,const FString& B){auto C=MoveTemp(Callback);if(C)C({true,Code!=0,Code,B,0});}
};
struct FSetup
{
    FRPGTradeLobbyContext Frame{TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Actor,true,true,true,true};
    TSharedRef<FFake> Fake=MakeShared<FFake>();TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FRPGEquipmentReadGuard Guard;uint64 Ticket=0;bool Blocked=false;int32 Applies=0;FRPGTradeAssets Cache;
    FSetup()
    {
        Owner=FRPGTradeLobbyOwner::Create(Fake,[this]{return Frame;});
        Owner->SetAssetSink([this]{if(Blocked)return false;Ticket=Guard.Capture();return true;},
            [this](const FRPGTradeAssets& A){if(Blocked||!Guard.Matches(Ticket))return false;Cache=A;++Applies;return true;},
            [this]{return !Blocked&&Guard.Matches(Ticket);});
        Owner->Tick();Fake->Reply(200,Envelope());
    }
    ~FSetup(){Owner->Shutdown();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsCodecTest,"ProjectRPG.Trading.Assets.Codec",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsCodecTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;const auto S=Completed();FRPGTradeAssets A;
    TestTrue(TEXT("coherent complete projection"),FRPGTradeAssets::Decode(Payload(),S,Actor,TEXT("76561198000000000"),A));
    if(!TestEqual(TEXT("inventory row"),A.Items.Num(),1)||!TestEqual(TEXT("mail row"),A.Mail.Num(),1)||!TestEqual(TEXT("wallet row"),A.Wallet.Balances.Num(),1))return false;
    TestEqual(TEXT("exact Gold"),A.Wallet.Balances[0].Balance,9007199254740993LL);TestEqual(TEXT("exact wallet revision"),A.Wallet.Balances[0].Revision,9007199254740995LL);
    TestEqual(TEXT("exact item revision"),A.Items[0].GetRevision(),9007199254740997LL);
    TestTrue(TEXT("empty complete inventory is valid"),FRPGTradeAssets::Decode(Payload(TEXT("")),S,Actor,TEXT("76561198000000000"),A));
    int32 Case=0;for(const auto& Bad:TArray<FString>{Payload().Replace(TEXT("\"isComplete\":true"),TEXT("\"isComplete\":false")),Payload().Replace(TEXT("\"isComplete\":true"),TEXT("\"isComplete\":true,\"isComplete\":true")),
        Payload().Replace(TEXT("9007199254740993"),TEXT("-1")),Payload().Replace(TEXT("9007199254740997"),TEXT("1.5")),Payload(Item()+TEXT(",")+Item()),
        Payload().Replace(TEXT("76561198000000000"),TEXT("76561198000000001")),Payload().Replace(*Id(Actor),*Id(Other))})
        TestFalse(FString::Printf(TEXT("invalid scope/precision/completeness/duplicates case %d"),Case++),FRPGTradeAssets::Decode(Bad,S,Actor,TEXT("76561198000000000"),A));
    auto Open=S;Open.Status=ERPGTradeStatus::Negotiating;TestFalse(TEXT("not proof of completion"),FRPGTradeAssets::Decode(Payload(),Open,Actor,TEXT("76561198000000000"),A));
    TArray<FString> Excess;for(int32 I=10;I<511;++I)Excess.Add(Item(I,true));TestFalse(TEXT("501 item cap"),FRPGTradeAssets::Decode(Payload(FString::Join(Excess,TEXT(","))),S,Actor,TEXT("76561198000000000"),A));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsOwnerTest,"ProjectRPG.Trading.Assets.OwnerAndGeneration",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsOwnerTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;FSetup S;S.Owner->Tick();TestEqual(TEXT("automatic first read"),S.Fake->Gets,1);S.Fake->Reply(200,Payload());
    TestEqual(TEXT("cache applied once"),S.Applies,1);TestFalse(TEXT("fresh summary"),S.Owner->GetView().AssetSummary.IsEmpty());
    S.Owner->Tick();TestEqual(TEXT("no repeated automatic import"),S.Fake->Gets,1);
    S.Guard.Invalidate();TestTrue(TEXT("newer cache hides old summary"),S.Owner->GetView().AssetSummary.IsEmpty());
    TestTrue(TEXT("explicit retry"),S.Owner->ReloadAssets());S.Guard.Invalidate();S.Fake->Reply(200,Payload());TestEqual(TEXT("superseded read cannot import"),S.Applies,1);
    S.Blocked=true;TestFalse(TEXT("unresolved equipment journal blocks capture"),S.Owner->ReloadAssets());S.Blocked=false;
    TestTrue(TEXT("retry after resolving blocking work"),S.Owner->ReloadAssets());S.Fake->Reply(503,TEXT("{}"));TestTrue(TEXT("failed read never shows zero wallet"),S.Owner->GetView().AssetSummary.IsEmpty());
    TestTrue(TEXT("completion remains after failed asset read"),S.Owner->GetClient().GetSession()->Status==ERPGTradeStatus::Completed);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsNewTradeScopeTest,"ProjectRPG.Trading.Assets.NewTradeScope",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsNewTradeScopeTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;FSetup S;S.Owner->Tick();S.Fake->Reply(200,Payload());
    TestFalse(TEXT("first completed trade has a fresh summary"),S.Owner->GetView().AssetSummary.IsEmpty());
    S.Owner->DismissClosed();
    if(!TestNull(TEXT("first trade dismissed"),S.Owner->GetClient().GetSession()))return false;
    const FGuid Next(9,9,9,9);
    auto ForNext=[&](FString Body){return Body.Replace(*Id(Trade),*Id(Next));};
    if(!TestTrue(TEXT("observe next trade"),S.Owner->Refresh()))return false;
    S.Fake->Reply(200,ForNext(Envelope(TEXT("Negotiating"))));
    if(!TestTrue(TEXT("observe next completion"),S.Owner->Refresh()))return false;
    auto NextCompleted=ForNext(Session()).Replace(TEXT("\"revision\":\"10\""),TEXT("\"revision\":\"11\""));
    S.Fake->Reply(200,TEXT("{\"session\":")+NextCompleted+TEXT(",\"observedAt\":\"2026-11-10T00:00:00Z\",\"isOpen\":false,\"allItemsAvailable\":true,\"initiatorItems\":[],\"inviteeItems\":[]}"));
    const auto* Current=S.Owner->GetClient().GetSession();
    if(!TestTrue(TEXT("new session completed before its first asset tick"),Current && Current->TradeId==Next && Current->Status==ERPGTradeStatus::Completed))return false;
    TestTrue(TEXT("previous trade cannot supply the new completion summary"),S.Owner->GetView().AssetSummary.IsEmpty());
    TestEqual(TEXT("no speculative cache import"),S.Applies,1);
    if(!TestTrue(TEXT("read assets for the new completion"),S.Owner->ReloadAssets()))return false;
    S.Fake->Reply(200,ForNext(Payload()).Replace(TEXT("\"revision\":\"10\""),TEXT("\"revision\":\"11\"")));
    TestEqual(TEXT("new trade assets imported once"),S.Applies,2);
    TestFalse(TEXT("new summary visible only after its own read"),S.Owner->GetView().AssetSummary.IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsLifetimeTest,"ProjectRPG.Trading.Assets.ScopeAndPending",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsLifetimeTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;
    {FSetup S;S.Owner->Tick();S.Frame.Character=Other;S.Fake->Reply(200,Payload());TestEqual(TEXT("selection changed during callback"),S.Applies,0);}
    {FSetup S;S.Owner->Tick();auto C=MoveTemp(S.Fake->Callback);S.Frame.Account=TEXT("76561198000000001");S.Fake->Account=S.Frame.Account;S.Owner->Tick();C({true,true,200,Payload(),0});TestEqual(TEXT("old account read ignored"),S.Applies,0);}
    {FSetup S;S.Owner->Tick();S.Frame.bAuthenticated=false;S.Fake->Reply(200,Payload());TestEqual(TEXT("expired auth read not applied"),S.Applies,0);}
    {auto F=MakeShared<FFake>();auto O=MakeShared<FRPGTradeCoordinator>(F);O->SelectCharacter(Actor);O->Refresh();F->Reply(200,Envelope(TEXT("Negotiating")));O->Submit(ERPGTradeAction::Confirm);F->Reply(0,TEXT(""));TestNotNull(TEXT("unconfirmed command retained"),O->GetClient().GetPending());TestFalse(TEXT("assets cannot resolve original pending confirm"),O->RefreshAssets([](FRPGTradeHttpResult){}));}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsWidgetTest,"ProjectRPG.Trading.Assets.WidgetAndClose",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsWidgetTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;FSetup S;auto* W=NewObject<URPGTradeWidget>();W->Initialize();W->SetTradeOwner(S.Owner.ToSharedRef());const auto Slate=W->TakeWidget();S.Owner->Tick();W->CloseScreen();
    TestNotNull(TEXT("closing during asset read keeps owner completion"),S.Owner->GetClient().GetSession());S.Fake->Reply(200,Payload());
    auto* Second=NewObject<URPGTradeWidget>();Second->Initialize();Second->SetTradeOwner(S.Owner.ToSharedRef());const auto Again=Second->TakeWidget();
    auto* Summary=Cast<UTextBlock>(Second->GetWidgetFromName(TEXT("TradeAssetSummary")));auto* Reload=Cast<UButton>(Second->GetWidgetFromName(TEXT("TradeAssetsReload")));
    if(!TestNotNull(TEXT("real summary"),Summary)||!TestNotNull(TEXT("real reload"),Reload))return false;
    const FString Text=Summary->GetText().ToString();TestTrue(TEXT("exact gold and counts shown"),Text.Contains(FText::AsNumber(9007199254740993LL).ToString())&&Text.Contains(TEXT("1")));
    TestFalse(TEXT("raw persistence data not in summary"),Text.Contains(TEXT("PrivateSource"))||Text.Contains(Id(Actor)));
    Reload->OnClicked.Broadcast();S.Fake->Reply(500,TEXT("{}"));TestTrue(TEXT("failure clears current summary"),Summary->GetText().IsEmpty());Second->CloseScreen();return true;
}
struct FRPGAssetsHttpRun
{TSharedPtr<FRPGTradeLobbyOwner> Owner;TSharedPtr<FRPGHttpTradeTransport> Transport;FRPGTradeLobbyContext Frame;FRPGTradeAssets Cache;FAutomationTestBase* Test=nullptr;int32 Applies=0;double Started=0;~FRPGAssetsHttpRun(){if(Owner)Owner->Shutdown();}};
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRPGAssetsHttpWait,TSharedPtr<FRPGAssetsHttpRun>,Run);
bool FRPGAssetsHttpWait::Update()
{
    if(FPlatformTime::Seconds()-Run->Started>30){Run->Test->AddError(TEXT("Asset HTTP timeout"));return true;}
    Run->Owner->Tick();if(!Run->Applies)return false;
    Run->Test->TestEqual(TEXT("actual HTTP precise wallet"),Run->Cache.Wallet.Balances[0].Balance,9007199254740993LL);
    Run->Test->TestEqual(TEXT("actual HTTP current item"),Run->Cache.Items[0].GetRevision(),9007199254740997LL);Run->Owner->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeAssetsHttpTest,"ProjectRPG.Trading.Assets.RealHttp",RPGTradeAssetsUiTests::Flags)
bool FRPGTradeAssetsHttpTest::RunTest(const FString&)
{
    using namespace RPGTradeAssetsUiTests;FString Endpoint;if(!FParse::Value(FCommandLine::Get(),TEXT("TradeTestEndpoint="),Endpoint)){AddError(TEXT("Fixture endpoint required"));return false;}
    auto Run=MakeShared<FRPGAssetsHttpRun>();Run->Test=this;Run->Started=FPlatformTime::Seconds();Endpoint+=TEXT("/assets-fixture");Run->Frame={Endpoint,TEXT("76561198000000000"),Actor,true,true,true,true};TWeakPtr<FRPGAssetsHttpRun> Weak=Run;
    Run->Transport=MakeShared<FRPGHttpTradeTransport>([Endpoint](FRPGTradeHttpContext& C){C={Endpoint,TEXT("76561198000000000"),TEXT("Bearer trade-test-player")};return true;},[](const FRPGTradeHttpContext&,int32){return true;});
    Run->Owner=FRPGTradeLobbyOwner::Create(Run->Transport.ToSharedRef(),[Weak]{return Weak.Pin()->Frame;});
    Run->Owner->SetAssetSink([]{return true;},[Weak](const FRPGTradeAssets& A){auto S=Weak.Pin();S->Cache=A;++S->Applies;return true;},[]{return true;});
    Run->Owner->Tick();ADD_LATENT_AUTOMATION_COMMAND(FRPGAssetsHttpWait(Run));return true;
}
#endif
