#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"
#include <limits>

namespace RPGTradeTargetUiTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid Actor(1,1,1,1),Target(2,2,2,2),Foreign(3,3,3,3);
const FString Name=TEXT("거래 상대&?+");
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Result(FGuid Who=Target,const FString& Label=Name)
{return FString::Printf(TEXT(R"({"targets":[{"characterId":"%s","name":"%s"}]})"),*Id(Who),*Label);}
struct FFake final : IRPGTradeTransport
{
    FString Endpoint=TEXT("http://localhost:3000/api"),Account=TEXT("76561198000000000"),Body,Action,Query;
    bool Token=true,Started=true;int32 Sends=0;
    TFunction<void(FRPGTradeHttpResult)> Callback;
    virtual bool GetContext(FString& E,FString& A)const override{E=Endpoint;A=Account;return true;}
    virtual bool CanSend()const override{return Token;}
    virtual bool Send(FGuid,FGuid,const FString& A,const FString& B,TFunction<void(FRPGTradeHttpResult)> C)override
    {++Sends;Action=A;Body=B;if(Started)Callback=MoveTemp(C);return Started;}
    virtual bool SendTargets(FGuid,const FString& N,TFunction<void(FRPGTradeHttpResult)> C)override
    {++Sends;Action=TEXT("targets");Query=N;if(Started)Callback=MoveTemp(C);return Started;}
    virtual void Cancel()override{Callback={};}
    void Reply(int32 Code,const FString& B=TEXT(""),double Delay=0,bool Current=true)
    {auto C=MoveTemp(Callback);if(C)C({Current,Code!=0,Code,B,Delay});}
};
struct FSetup
{
    double Time=100;
    FRPGTradeLobbyContext Frame{TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Actor,true,true,true,true};
    TSharedRef<FFake> Fake=MakeShared<FFake>();TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FSetup(){Owner=FRPGTradeLobbyOwner::Create(Fake,[this]{return Frame;},[this]{return Time;});Owner->Tick();Fake->Reply(200,TEXT("{\"session\":null}"));}
    bool Search(){const auto V=Owner->GetTargetView();return Owner->SetTargetName(V.InputToken,Name)&&Owner->SearchTargets(V.InputToken);}
    void Found(){Search();Fake->Reply(200,Result());}
    ~FSetup(){Owner->Shutdown();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeTargetCodecTest,"ProjectRPG.Trading.Targets.Codec",RPGTradeTargetUiTests::Flags)
bool FRPGTradeTargetCodecTest::RunTest(const FString&)
{
    using namespace RPGTradeTargetUiTests;FString Normal;
    TestTrue(TEXT("trimmed Unicode and reserved query characters"),RPGTradeTargets::NormalizeName(TEXT("  ")+Name+TEXT("  "),Normal));TestEqual(TEXT("preserved query"),Normal,Name);
    for(const auto& Bad:TArray<FString>{TEXT("a"),TEXT("ab\ncd"),FString::ChrN(21,'x'),FString::ChrN(65,' ')})TestFalse(TEXT("bounded name"),RPGTradeTargets::NormalizeName(Bad,Normal));
    TArray<FRPGTradeTarget> Rows;TestTrue(TEXT("typed minimal candidate"),RPGTradeTargets::Decode(Result(),Actor,Name,Rows));TestEqual(TEXT("one candidate"),Rows.Num(),1);
    const auto Good=Rows;
    for(const auto& Bad:TArray<FString>{Result(Actor),Result(FGuid()),Result(Target,TEXT("Different")),Result(Target,TEXT(" padded ")),
        TEXT("{\"targets\":null}"),TEXT("{\"targets\":[],\"targets\":[]}"),TEXT("{\"targets\":[],\"account\":\"private\"}"),
        Result().Replace(TEXT("\"name\":"),TEXT("\"account\":\"private\",\"name\":")),FString::ChrN(4097,'x')})
    {TestFalse(TEXT("malformed, private, different or self candidate rejected"),RPGTradeTargets::Decode(Bad,Actor,Name,Rows));TestEqual(TEXT("failure cannot partially replace output"),Rows.Num(),Good.Num());}
    const FString Row=Result().Mid(12,Result().Len()-14);
    TestFalse(TEXT("multiple candidates rejected"),RPGTradeTargets::Decode(TEXT("{\"targets\":[")+Row+TEXT(",")+Row+TEXT("]}"),Actor,Name,Rows));
    TestTrue(TEXT("empty result is valid"),RPGTradeTargets::Decode(TEXT("{\"targets\":[]}"),Actor,Name,Rows));TestTrue(TEXT("no synthetic target"),Rows.IsEmpty());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeTargetSelectionTest,"ProjectRPG.Trading.Targets.SelectionAndReplay",RPGTradeTargetUiTests::Flags)
bool FRPGTradeTargetSelectionTest::RunTest(const FString&)
{
    using namespace RPGTradeTargetUiTests;FSetup S;S.Found();const auto V=S.Owner->GetTargetView();
    TestTrue(TEXT("candidate can be explicitly selected"),V.bCanInvite&&V.ResultToken.IsValid());
    TestFalse(TEXT("arbitrary character rejected"),S.Owner->InviteTarget(V.ResultToken,Foreign));TestFalse(TEXT("old UI token rejected"),S.Owner->InviteTarget(FGuid::NewGuid(),Target));
    TestTrue(TEXT("exact selected candidate starts intent"),S.Owner->InviteTarget(V.ResultToken,Target));
    if(!TestNotNull(TEXT("invite intent exists"),S.Owner->GetClient().GetPending()))return false;
    const auto Pending=*S.Owner->GetClient().GetPending();TestTrue(TEXT("selected id in original intent"),Pending.Body.Contains(Id(Target)));
    S.Fake->Reply(0);TestFalse(TEXT("unknown invite cannot be replaced"),S.Owner->InviteTarget(V.ResultToken,Target));
    TestFalse(TEXT("unknown invite cannot search"),S.Owner->SearchTargets(V.InputToken));S.Time=102;S.Owner->Tick();
    TestEqual(TEXT("retry identical bytes"),S.Fake->Body,Pending.Body);TestTrue(TEXT("retry identical request id"),S.Owner->GetClient().GetPending()->RequestId==Pending.RequestId);
    TestFalse(TEXT("discovery hidden while unknown"),S.Owner->GetTargetView().bVisible);
    FSetup Age;Age.Search();Age.Time=110;Age.Fake->Reply(200,Result());const auto Before=Age.Owner->GetTargetView();Age.Time=129;
    TestTrue(TEXT("age starts at dispatch"),Age.Owner->GetTargetView().bCanInvite);Age.Time=130;TestFalse(TEXT("30 second expiry"),Age.Owner->GetTargetView().bCanInvite);
    Age.Time=100;TestFalse(TEXT("clock rollback cannot revive results"),Age.Owner->GetTargetView().bCanInvite);
    TestFalse(TEXT("old result token cannot dispatch"),Age.Owner->InviteTarget(Before.ResultToken,Target));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeTargetContextTest,"ProjectRPG.Trading.Targets.ContextAndThrottle",RPGTradeTargetUiTests::Flags)
bool FRPGTradeTargetContextTest::RunTest(const FString&)
{
    using namespace RPGTradeTargetUiTests;
    {FSetup S;S.Search();const auto Input=S.Owner->GetTargetView().InputToken;S.Owner->SetTargetName(Input,TEXT("Changed"));S.Fake->Reply(200,Result());TestFalse(TEXT("edited query discards older response"),S.Owner->GetTargetView().bCanInvite);S.Time=102;TestTrue(TEXT("old callback releases busy state"),S.Owner->GetTargetView().bCanSearch);}
    {FSetup S;S.Found();const auto Old=S.Owner->GetTargetView();S.Frame.Character=Foreign;TestFalse(TEXT("selection change at send is observed"),S.Owner->InviteTarget(Old.ResultToken,Target));TestTrue(TEXT("new selection has no inherited query"),S.Owner->GetTargetView().Query.IsEmpty());TestFalse(TEXT("stale input event ignored"),S.Owner->SetTargetName(Old.InputToken,Name));}
    {FSetup S;S.Search();auto Old=MoveTemp(S.Fake->Callback);S.Frame.Account=TEXT("76561198000000001");S.Fake->Account=S.Frame.Account;S.Owner->Tick();Old({true,true,200,Result(),0});TestFalse(TEXT("old account response ignored"),S.Owner->GetTargetView().bCanInvite);}
    {FSetup S;S.Search();S.Frame.bAuthenticated=false;S.Owner->Tick();S.Fake->Reply(200,Result());S.Frame.bAuthenticated=true;S.Owner->Tick();TestFalse(TEXT("reauthentication requires new discovery"),S.Owner->GetTargetView().bCanInvite);}
    {FSetup S;S.Search();S.Frame.bLocalLobby=false;S.Owner->Tick();S.Fake->Reply(200,Result());S.Frame.bLocalLobby=true;S.Owner->Tick();TestFalse(TEXT("lobby return has no old candidate"),S.Owner->GetTargetView().bCanInvite);}
    {FSetup S;S.Search();S.Fake->Reply(429,TEXT("{}"),60);const auto Input=S.Owner->GetTargetView().InputToken;S.Owner->SetTargetName(Input,TEXT("Changed"));S.Time=159;TestFalse(TEXT("query edit cannot reset server delay"),S.Owner->GetTargetView().bCanSearch);S.Time=160;TestTrue(TEXT("explicit search after delay"),S.Owner->GetTargetView().bCanSearch);}
    {FSetup S;S.Fake->Started=false;TestFalse(TEXT("synchronous dispatch failure"),S.Search());S.Time=102;TestTrue(TEXT("failed dispatch releases busy"),S.Owner->GetTargetView().bCanSearch);}
    {FSetup S;S.Search();S.Fake->Reply(200,Result(),0,false);TestFalse(TEXT("stale credential response not adopted"),S.Owner->GetTargetView().bCanInvite);S.Time=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("nonfinite clock cannot search"),S.Owner->GetTargetView().bCanSearch);}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeTargetWidgetTest,"ProjectRPG.Trading.Targets.WidgetSelection",RPGTradeTargetUiTests::Flags)
bool FRPGTradeTargetWidgetTest::RunTest(const FString&)
{
    using namespace RPGTradeTargetUiTests;FSetup S;
    auto Make=[&](){auto* W=NewObject<URPGTradeWidget>();W->Initialize();W->SetTradeOwner(S.Owner.ToSharedRef());return W;};
    auto* W=Make();const auto Slate=W->TakeWidget();
    auto* Input=Cast<UEditableTextBox>(W->GetWidgetFromName(TEXT("TradeTargetName")));auto* Search=Cast<UButton>(W->GetWidgetFromName(TEXT("TradeTargetSearch")));auto* Invite=Cast<UButton>(W->GetWidgetFromName(TEXT("TradeTargetInvite")));auto* Choices=Cast<UComboBoxString>(W->GetWidgetFromName(TEXT("TradeTargetChoices")));
    if(!TestNotNull(TEXT("actual name input"),Input)||!TestNotNull(TEXT("actual search button"),Search)||!TestNotNull(TEXT("actual invite button"),Invite)||!TestNotNull(TEXT("actual candidate combo"),Choices))return false;
    Input->SetText(FText::FromString(Name));Input->OnTextChanged.Broadcast(Input->GetText());Search->OnClicked.Broadcast();S.Fake->Reply(200,Result());
    TestEqual(TEXT("public name only"),Choices->GetOptionAtIndex(0),Name);TestFalse(TEXT("no implicit selection"),Invite->GetIsEnabled());
    Choices->SetSelectedIndex(0);Choices->OnSelectionChanged.Broadcast(Name,ESelectInfo::Direct);TestTrue(TEXT("explicit choice enables invite"),Invite->GetIsEnabled());
    auto* Reopened=Make();const auto Again=Reopened->TakeWidget();auto* NewInvite=Cast<UButton>(Reopened->GetWidgetFromName(TEXT("TradeTargetInvite")));
    TestFalse(TEXT("new window requires own explicit selection"),NewInvite->GetIsEnabled());
    Invite->OnClicked.Broadcast();S.Fake->Reply(0);const auto Pending=*S.Owner->GetClient().GetPending();W->CloseScreen();Reopened->CloseScreen();
    auto* Last=Make();const auto LastSlate=Last->TakeWidget();TestTrue(TEXT("closed views preserve original intent"),S.Owner->GetClient().GetPending()->RequestId==Pending.RequestId);
    S.Time=102;S.Owner->Tick();TestEqual(TEXT("reopened view uses same retry body"),S.Fake->Body,Pending.Body);Last->CloseScreen();return true;
}
struct FRPGTargetUiHttpRun
{
    FAutomationTestBase* Test=nullptr;TSharedPtr<FRPGTradeLobbyOwner> Owner;TSharedPtr<FRPGHttpTradeTransport> Transport;
    FRPGTradeLobbyContext Frame;double Started=0,Time=100;int32 Step=0;FString Original;
    ~FRPGTargetUiHttpRun(){if(Owner)Owner->Shutdown();}
};
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRPGTargetUiHttpWait,TSharedPtr<FRPGTargetUiHttpRun>,Run);
bool FRPGTargetUiHttpWait::Update()
{
    using namespace RPGTradeTargetUiTests;
    if(FPlatformTime::Seconds()-Run->Started>30){Run->Test->AddError(TEXT("Target UI HTTP timeout"));Run->Owner->Shutdown();return true;}
    const auto View=Run->Owner->GetTargetView();
    if(Run->Step==0 && View.bCanEdit)
    {Run->Owner->SetTargetName(View.InputToken,Name);Run->Test->TestTrue(TEXT("actual HTTP discovery sent"),Run->Owner->SearchTargets(View.InputToken));Run->Step=1;}
    else if(Run->Step==1 && View.bCanInvite)
    {Run->Test->TestEqual(TEXT("decoded name from escaped GET"),View.Targets[0].Name,Name);Run->Test->TestTrue(TEXT("actual selected invite sent"),Run->Owner->InviteTarget(View.ResultToken,Target));Run->Original=Run->Owner->GetClient().GetPending()->Body;Run->Step=2;}
    else if(Run->Step==2)
    {
        const auto* Pending=Run->Owner->GetClient().GetPending();
        if(Pending && Pending->Delivery==ERPGTradeDelivery::Unknown){Run->Time=102;Run->Owner->Tick();Run->Test->TestEqual(TEXT("real HTTP retry uses immutable invite"),Run->Owner->GetClient().GetPending()->Body,Run->Original);Run->Step=3;}
    }
    else if(Run->Step==3 && !Run->Owner->GetClient().GetPending())
    {const auto* Session=Run->Owner->GetClient().GetSession();Run->Test->TestTrue(TEXT("selected recipient confirmed by persisted receipt"),Session&&Session->InviteeCharacterId==Target);Run->Owner->Shutdown();return true;}
    return false;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradeTargetHttpTest,"ProjectRPG.Trading.Targets.RealHttp",RPGTradeTargetUiTests::Flags)
bool FRPGTradeTargetHttpTest::RunTest(const FString&)
{
    using namespace RPGTradeTargetUiTests;FString Endpoint;if(!FParse::Value(FCommandLine::Get(),TEXT("TradeTestEndpoint="),Endpoint)){AddError(TEXT("Fixture endpoint required"));return false;}
    AddExpectedMessagePlain(TEXT("request failed, libcurl error: 52 (Server returned nothing (no headers, no data))"),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
    AddExpectedMessagePlain(TEXT("libcurl info message cache "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,0);
    AddExpectedMessage(TEXT("^[0-9A-Fa-f]+ POST http://127\\.0\\.0\\.1:[0-9]+/api/characters/[0-9a-f-]+/trades/[0-9a-f-]+/invite completed with reason 'Other' after "),ELogVerbosity::Warning,EAutomationExpectedMessageFlags::Contains,1);
    auto Run=MakeShared<FRPGTargetUiHttpRun>();Run->Test=this;Run->Started=FPlatformTime::Seconds();Run->Frame={Endpoint,TEXT("76561198000000000"),Actor,true,true,true,true};
    Run->Transport=MakeShared<FRPGHttpTradeTransport>([Endpoint](FRPGTradeHttpContext& C){C={Endpoint,TEXT("76561198000000000"),TEXT("Bearer trade-test-player")};return true;},[](const FRPGTradeHttpContext&,int32){return true;});
    TWeakPtr<FRPGTargetUiHttpRun> Weak=Run;
    Run->Owner=FRPGTradeLobbyOwner::Create(Run->Transport.ToSharedRef(),[Weak]{auto S=Weak.Pin();return S?S->Frame:FRPGTradeLobbyContext{};},[Weak]{auto S=Weak.Pin();return S?S->Time:0;});
    Run->Owner->Tick();ADD_LATENT_AUTOMATION_COMMAND(FRPGTargetUiHttpWait(Run));return true;
}
#endif
