#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "UI/Lobby/RPGTradeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace RPGTradePreviewTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid First(1,1,1,1), Second(2,2,2,2), Trade(3,3,3,3), Item(4,4,4,4);
using FObject=TSharedPtr<FJsonObject>;
FString Id(FGuid G) { return G.ToString(EGuidFormats::DigitsWithHyphensLower); }
FObject Parse(const FString& Text) { FObject O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O); return O; }
FString Json(const FObject& O) { FString T; FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&T)); return T; }
FObject Session(int32 Revision=5)
{
    return Parse(FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"%d","offerRevision":"1","expiresAt":"2030-01-01T00:00:00Z","status":"Negotiating","completed":false,"initiatorOffer":{"currencyAmount":"9007199254740993","items":[{"itemId":"%s","expectedRevision":"9007199254740995","quantity":2}]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":false,"inviteeReady":false,"initiatorConfirmed":false,"inviteeConfirmed":false})"),*Id(Trade),*Id(First),*Id(Second),Revision,*Id(Item)));
}
FObject Preview(const FObject& S)
{
    const FString Detail=TEXT(R"({"definitionType":"ItemDefinition","definitionName":"Sword","definitionVersion":1,"instanceTags":["Affix.Rare"],"statValues":[{"statTag":"Attack","value":12.5}],"equipmentProgression":{"growthBandId":"Bronze","ruleVersion":1,"level":2},"bindState":"Unbound","durability":{"current":97,"maximum":100},"expiresAtUtc":null})");
    auto O=MakeShared<FJsonObject>(); O->SetObjectField(TEXT("session"),S); O->SetStringField(TEXT("observedAt"),TEXT("2026-11-08T00:00:00Z"));
    O->SetBoolField(TEXT("isOpen"),true); O->SetBoolField(TEXT("allItemsAvailable"),true);
    auto I=MakeShared<FJsonObject>(); I->SetStringField(TEXT("itemId"),Id(Item)); I->SetStringField(TEXT("expectedRevision"),TEXT("9007199254740995")); I->SetNumberField(TEXT("quantity"),2);
    I->SetStringField(TEXT("availability"),TEXT("Available")); I->SetObjectField(TEXT("details"),Parse(Detail));
    O->SetArrayField(TEXT("initiatorItems"),{MakeShared<FJsonValueObject>(I)}); O->SetArrayField(TEXT("inviteeItems"),{}); return O;
}
FObject FirstItem(const FObject& P) { return P->GetArrayField(TEXT("initiatorItems"))[0]->AsObject(); }
FString Envelope(const FObject& S) { auto O=MakeShared<FJsonObject>(); O->SetObjectField(TEXT("session"),S); return Json(O); }
FRPGTradeSession DecodeSession(const FObject& S)
{ FRPGTradeClient C; C.Bind(TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Second); C.CompleteRead(C.BeginRead(),200,Envelope(S)); return *C.GetSession(); }
struct FFake final : IRPGTradeTransport
{
    FString Body,Action;
    int32 Sends=0,Previews=0;
    TFunction<void(FRPGTradeHttpResult)> Callback;
    virtual bool GetContext(FString& E,FString& A) const override { E=TEXT("http://localhost:3000/api"); A=TEXT("76561198000000000");return true; }
    virtual bool CanSend() const override {return true;}
    virtual bool Send(FGuid,FGuid,const FString& A,const FString& B,TFunction<void(FRPGTradeHttpResult)> C) override {++Sends;Action=A;Body=B;Callback=MoveTemp(C);return true;}
    virtual bool SendPreview(FGuid,FGuid,TFunction<void(FRPGTradeHttpResult)> C) override {++Sends;++Previews;Action=TEXT("preview");Callback=MoveTemp(C);return true;}
    virtual void Cancel() override {Callback={};}
    void Reply(int32 Code,const FString& B=TEXT("")) {auto C=MoveTemp(Callback);if(C)C({true,Code!=0,Code,B,0});}
};
struct FSetup
{
    double Time=100; bool bKnown=true;
    FString Label=TEXT("검");
    FRPGTradeLobbyContext Frame{TEXT("http://localhost:3000/api"),TEXT("76561198000000000"),Second,true,true,true,true};
    TSharedRef<FFake> Fake=MakeShared<FFake>(); TSharedPtr<FRPGTradeLobbyOwner> Owner;
    FSetup()
    {
        Owner=FRPGTradeLobbyOwner::Create(Fake,[this]{return Frame;},[this]{return Time;});
        Owner->SetDefinitionResolver([this](const FRPGTradeItemDetails& D,FText& Name,FText& Description)
        { Name=FText::FromString(Label);Description=FText::FromString(TEXT("공격력 12.5 · 희귀 · 강화 +2 · 내구도 97/100"));return bKnown && D.DefinitionType==TEXT("ItemDefinition") && D.DefinitionName==TEXT("Sword") && D.DefinitionVersion==1; });
    }
    void Load() {Owner->Tick();Fake->Reply(200,Envelope(Session()));Time=102;Owner->Tick();Fake->Reply(200,Json(Preview(Session())));}
    ~FSetup(){Owner->Shutdown();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewCodecTest,"ProjectRPG.Trading.Preview.Codec",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewCodecTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    const auto S=DecodeSession(Session());FRPGTradePreview Out;
    TestTrue(TEXT("full public details decode"),FRPGTradePreview::Decode(Json(Preview(Session())),Second,S,Out));
    TestEqual(TEXT("exact large item revision"),Out.InitiatorItems[0].Offered.ExpectedRevision,int64(9007199254740995LL));
    TestEqual(TEXT("stat value"),Out.InitiatorItems[0].Details->Stats[0].Value,12.5);
    for(int32 Case=0;Case<7;++Case)
    {
        auto P=Preview(Session()); auto I=FirstItem(P);auto D=I->GetObjectField(TEXT("details"));
        if(Case==0)I->SetStringField(TEXT("expectedRevision"),TEXT("9007199254740994"));
        if(Case==1)I->SetNumberField(TEXT("quantity"),3);
        if(Case==2)D->GetObjectField(TEXT("durability"))->SetNumberField(TEXT("current"),101);
        if(Case==3)D->SetStringField(TEXT("bindState"),TEXT("CharacterBound"));
        if(Case==4)D->SetStringField(TEXT("expiresAtUtc"),TEXT("2020-01-01T00:00:00Z"));
        if(Case==5)P->SetBoolField(TEXT("allItemsAvailable"),false);
        if(Case==6)P->SetStringField(TEXT("isOpen"),TEXT("true"));
        TestFalse(TEXT("malformed/mismatched details fail closed"),FRPGTradePreview::Decode(Json(P),Second,S,Out));
    }
    FString Duplicate=Json(Preview(Session())).Replace(TEXT("\"isOpen\": true"),TEXT("\"isOpen\": true,\"isOpen\": false"));
    TestFalse(TEXT("duplicate field rejected"),FRPGTradePreview::Decode(Duplicate,Second,S,Out));
    TestFalse(TEXT("oversized preview rejected"),FRPGTradePreview::Decode(FString::ChrN(131073,' '),Second,S,Out));
    auto Unavailable=Preview(Session()); FirstItem(Unavailable)->SetStringField(TEXT("availability"),TEXT("Unavailable"));FirstItem(Unavailable)->SetField(TEXT("details"),MakeShared<FJsonValueNull>());Unavailable->SetBoolField(TEXT("allItemsAvailable"),false);
    TestTrue(TEXT("generic unavailable preserves no details"),FRPGTradePreview::Decode(Json(Unavailable),Second,S,Out));
    TestFalse(TEXT("unavailable does not claim availability"),Out.bAllAvailable);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewVersionTest,"ProjectRPG.Trading.Preview.Versioning",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewVersionTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    double Time=100;auto Fake=MakeShared<FFake>();auto C=MakeShared<FRPGTradeCoordinator>(Fake,[&]{return Time;});
    C->SelectCharacter(Second);C->Refresh();Fake->Reply(200,Envelope(Session()));C->RefreshPreview();Fake->Reply(200,Json(Preview(Session())));
    const auto Token=C->GetPreviewToken(); TestTrue(TEXT("preview token"),Token.IsValid());
    C->RefreshPreview();Fake->Reply(200,Json(Preview(Session())));TestTrue(TEXT("unchanged data retains review identity"),C->GetPreviewToken()==Token);
    auto Changed=Preview(Session());FirstItem(Changed)->GetObjectField(TEXT("details"))->GetObjectField(TEXT("durability"))->SetNumberField(TEXT("current"),96);
    C->RefreshPreview();Fake->Reply(200,Json(Changed));TestTrue(TEXT("changed detail with the same session still requires new review"),C->GetPreviewToken()!=Token);
    C->Refresh();Fake->Reply(200,Envelope(Session(6)));TestNull(TEXT("new session invalidates earlier detail view"),C->GetPreview());
    C->RefreshPreview();Fake->Reply(200,Json(Preview(Session())));TestNull(TEXT("stale preview cannot overwrite latest session"),C->GetPreview());
    TestEqual(TEXT("session does not regress"),C->GetClient().GetSession()->Revision,int64(6));
    C->RefreshPreview();auto Late=Fake->Callback;C->SelectCharacter(First);Late({true,true,200,Json(Preview(Session(6))),0});
    TestNull(TEXT("old selection callback ignored"),C->GetPreview());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewConsentTest,"ProjectRPG.Trading.Preview.Consent",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewConsentTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    FSetup S;S.Load();const auto View=S.Owner->GetView();
    TestTrue(TEXT("known current item enables explicit review"),View.bCanReview && View.bReady && View.ReviewToken.IsValid());
    TestFalse(TEXT("no token means no consent"),S.Owner->SubmitConsent(ERPGTradeAction::Ready,{}));
    TestTrue(TEXT("reviewed ready dispatch"),S.Owner->SubmitConsent(ERPGTradeAction::Ready,View.ReviewToken));
    const auto Request=*S.Owner->GetClient().GetPending();auto B=Parse(S.Fake->Body);
    TestEqual(TEXT("consent pins session revision"),B->GetStringField(TEXT("expectedRevision")),FString(TEXT("5")));
    TestEqual(TEXT("consent pins offer revision"),B->GetStringField(TEXT("expectedOfferRevision")),FString(TEXT("1")));
    auto Ready=Session(6);Ready->SetBoolField(TEXT("inviteeReady"),true);
    auto Receipt=MakeShared<FJsonObject>();Receipt->SetStringField(TEXT("requestId"),Id(Request.RequestId));Receipt->SetObjectField(TEXT("session"),Ready);Receipt->SetField(TEXT("rejection"),MakeShared<FJsonValueNull>());Receipt->SetStringField(TEXT("committedAt"),TEXT("2026-11-08T00:00:00Z"));S.Fake->Reply(200,Json(Receipt));
    TestFalse(TEXT("ready response requires another current preview"),S.Owner->GetView().bConfirm);
    auto Both=Session(7);Both->SetBoolField(TEXT("inviteeReady"),true);Both->SetBoolField(TEXT("initiatorReady"),true);
    S.Owner->Refresh();S.Fake->Reply(200,Json(Preview(Both)));const auto Final=S.Owner->GetView();
    TestTrue(TEXT("both ready permits reviewed final confirmation"),Final.bConfirm);
    TestFalse(TEXT("old review cannot confirm newer snapshot"),S.Owner->SubmitConsent(ERPGTradeAction::Confirm,View.ReviewToken));
    TestTrue(TEXT("fresh reviewed confirmation"),S.Owner->SubmitConsent(ERPGTradeAction::Confirm,Final.ReviewToken));
    const auto Body=S.Fake->Body;S.Fake->Reply(0);S.Time=104;S.Owner->Tick();
    TestEqual(TEXT("uncertain final confirmation replays original bytes without new consent"),S.Fake->Body,Body);
    TestFalse(TEXT("new consent cannot replace uncertain confirmation"),S.Owner->SubmitConsent(ERPGTradeAction::Confirm,Final.ReviewToken));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewSafetyTest,"ProjectRPG.Trading.Preview.FreshnessAndDefinitions",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewSafetyTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    FSetup S;S.Load();const auto Token=S.Owner->GetView().ReviewToken;S.bKnown=false;
    TestFalse(TEXT("unknown local definition blocks review"),S.Owner->GetView().bCanReview);
    TestFalse(TEXT("definition loss invalidates previously reviewed token"),S.Owner->SubmitConsent(ERPGTradeAction::Ready,Token));
    S.bKnown=true;const auto Current=S.Owner->GetView().ReviewToken;TestTrue(TEXT("definition recovery requires a new explicit review"),Current.IsValid() && Current!=Token);
    S.Label=TEXT("양손검");const auto Renamed=S.Owner->GetView().ReviewToken;
    TestTrue(TEXT("changed local presentation invalidates existing review"),Renamed.IsValid() && Renamed!=Current);
    S.Time=133;TestFalse(TEXT("expired preview freshness blocks consent"),S.Owner->SubmitConsent(ERPGTradeAction::Ready,Renamed));
    S.Owner->Refresh();auto P=Preview(Session());FirstItem(P)->GetObjectField(TEXT("details"))->SetNumberField(TEXT("definitionVersion"),2);S.Fake->Reply(200,Json(P));
    TestFalse(TEXT("different definition version cannot use known name"),S.Owner->GetView().bCanReview);
    TestFalse(TEXT("internal definition IDs not printed as fallback"),S.Owner->GetView().OtherDetails.ToString().Contains(TEXT("Sword")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewWidgetTest,"ProjectRPG.Trading.Preview.WidgetReview",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewWidgetTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    FSetup S;S.Load();auto* W=NewObject<URPGTradeWidget>();W->Initialize();W->SetTradeOwner(S.Owner.ToSharedRef());
    const auto SlateWidget=W->TakeWidget(); // The viewport normally retains this; do not destroy the test view before clicking it.
    auto* Check=Cast<UCheckBox>(W->GetWidgetFromName(TEXT("TradeReview")));auto* Ready=Cast<UButton>(W->GetWidgetFromName(TEXT("TradeReady")));
    if(!TestNotNull(TEXT("actual review checkbox"),Check)||!TestNotNull(TEXT("actual ready button"),Ready))return false;
    TestFalse(TEXT("ready starts unchecked"),Ready->GetIsEnabled());Check->SetIsChecked(true);Check->OnCheckStateChanged.Broadcast(true);
    TestTrue(TEXT("explicit check enables ready"),Ready->GetIsEnabled());
    auto Other=Preview(Session(6));FirstItem(Other)->GetObjectField(TEXT("details"))->GetObjectField(TEXT("durability"))->SetNumberField(TEXT("current"),96);
    S.Owner->Refresh();S.Fake->Reply(200,Json(Other));
    TestFalse(TEXT("changed snapshot resets actual checkbox"),Check->IsChecked());TestFalse(TEXT("changed snapshot requires review again"),Ready->GetIsEnabled());
    W->CloseScreen();return true;
}
struct FRPGPreviewHttpRun {FAutomationTestBase* Test=nullptr;TSharedPtr<FRPGHttpTradeTransport> Transport;double Started=0;bool Done=false;};
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRPGPreviewHttpWait,TSharedPtr<FRPGPreviewHttpRun>,Run);
bool FRPGPreviewHttpWait::Update(){if(FPlatformTime::Seconds()-Run->Started>30){Run->Test->AddError(TEXT("Preview HTTP timed out"));Run->Transport->Cancel();return true;}return Run->Done;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTradePreviewHttpTest,"ProjectRPG.Trading.Preview.RealHttp",RPGTradePreviewTests::Flags)
bool FRPGTradePreviewHttpTest::RunTest(const FString&)
{
    using namespace RPGTradePreviewTests;
    FString Endpoint;if(!FParse::Value(FCommandLine::Get(),TEXT("TradeTestEndpoint="),Endpoint)){AddError(TEXT("Fixture endpoint required"));return false;}
    auto Run=MakeShared<FRPGPreviewHttpRun>();Run->Test=this;Run->Started=FPlatformTime::Seconds();
    Run->Transport=MakeShared<FRPGHttpTradeTransport>([Endpoint](FRPGTradeHttpContext& O){O={Endpoint,TEXT("76561198000000000"),TEXT("Bearer trade-test-player")};return true;},[](const FRPGTradeHttpContext&,int32){return true;});
    TWeakPtr<FRPGPreviewHttpRun> Weak=Run;
    TestTrue(TEXT("real GET preview starts"),Run->Transport->SendPreview(First,Trade,[Weak,Endpoint](FRPGTradeHttpResult R)
    {if(auto State=Weak.Pin()){FRPGTradeClient C;C.Bind(Endpoint,TEXT("76561198000000000"),First);const auto Result=C.CompleteRead(C.BeginRead(Trade),R.StatusCode,R.Body,131072);FRPGTradePreview P;
        State->Test->TestTrue(TEXT("authenticated real preview wire codec"),R.bCurrentPlayerContext&&R.bTransportSuccessful&&Result==ERPGTradeResponse::Applied&&C.GetSession()&&FRPGTradePreview::Decode(R.Body,First,*C.GetSession(),P));State->Done=true;}}));
    ADD_LATENT_AUTOMATION_COMMAND(FRPGPreviewHttpWait(Run));return true;
}
#endif
