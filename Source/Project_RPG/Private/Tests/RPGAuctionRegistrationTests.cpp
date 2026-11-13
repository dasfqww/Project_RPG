#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGAuctionWidget.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

struct FRPGAuctionRegistrationWidgetTestAccess
{
    static void Policy(URPGAuctionWidget* W){W->PolicyButton->OnClicked.Broadcast();}
    static void Bag(URPGAuctionWidget* W){W->BagButton->OnClicked.Broadcast();}
    static void Select(URPGAuctionWidget* W){W->BagRows->SetSelectedIndex(0);}
    static void Inputs(URPGAuctionWidget* W){W->QuantityInput->SetText(FText::FromString(TEXT("4")));W->PriceInput->SetText(FText::FromString(LexToString(MAX_int64)));W->DurationInput->SetText(FText::FromString(TEXT("3600")));}
    static void Review(URPGAuctionWidget* W){W->RegisterButton->OnClicked.Broadcast();}
    static void Confirm(URPGAuctionWidget* W){W->ConfirmButton->OnClicked.Broadcast();}
    static void Edit(URPGAuctionWidget* W){W->PriceInput->SetText(FText::FromString(TEXT("699")));W->PriceInput->OnTextChanged.Broadcast(W->PriceInput->GetText());}
};
namespace AuctionRegistrationTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FString Load(const TCHAR* Name){FString S;FFileHelper::LoadFileToString(S,*(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_WIRE_FIXTURE"))/(FString(Name)+TEXT(".json"))));return S;}
TSharedPtr<FJsonObject> Obj(const FString& S){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O,FJsonSerializer::EFlags::StoreNumbersAsStrings);return O;}
FString Json(TSharedPtr<FJsonObject> O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
FGuid Id(const TCHAR* Key){FGuid G;FGuid::Parse(Obj(Load(TEXT("identity")))->GetStringField(Key),G);return G;}
FRPGAuctionLobbyContext Context(){return {TEXT("https://backend.example/api"),TEXT("76561198880000001"),Id(TEXT("sellerCharacterId")),true,true,true,true};}
FString Root(const TCHAR* Name){FString R;FParse::Value(FCommandLine::Get(),TEXT("AuctionJournalTestRoot="),R);return R/Name;}
FRPGTradeBagSnapshot Bag(const FRPGAuctionLobbyContext& C){FRPGTradeBagSnapshot B;B.Endpoint=C.Endpoint;B.Account=C.Account;B.Character=C.Character;B.Token=FGuid::NewGuid();B.ValidUntil=500;B.bReady=true;B.Items.Add({Id(TEXT("itemId")),1,10,FText::FromString(TEXT("검사용 광석 · 가방 1칸")),true});return B;}
FRPGAuctionRegistrationPolicy Policy(){FRPGAuctionRegistrationPolicy P;RPGAuction::DecodeRegistrationPolicy(Load(TEXT("policy")),P);return P;}
struct FFake final:IRPGAuctionTransport
{
    FRPGAuctionLobbyContext& C;FCompletion Done;FRPGAuctionIntent Last;int32 Gets=0,Posts=0;
    TFunction<void(const FRPGAuctionIntent&)> BeforeSend;
    explicit FFake(FRPGAuctionLobbyContext& In):C(In){}
    virtual bool GetContext(FString& E,FString& A) const override{E=C.Endpoint;A=C.Account;return !A.IsEmpty();}
    virtual bool CanSend() const override{return C.bAuthenticated && !Done;}
    virtual bool SendIntent(const FRPGAuctionIntent& I,FCompletion F) override{if(BeforeSend)BeforeSend(I);Last=I;++Posts;Done=MoveTemp(F);return true;}
    virtual bool SendRegistrationPolicy(FGuid,FCompletion F) override{++Gets;Done=MoveTemp(F);return true;}
    virtual bool SendPage(FGuid,const FRPGAuctionSearch&,bool,FCompletion) override{return false;}
    virtual bool SendListing(FGuid,FGuid,FCompletion) override{return false;}
    virtual bool SendPayable(FGuid,FGuid,FCompletion) override{return false;}
    virtual bool SendReceipt(FGuid,FGuid,FCompletion F) override{++Gets;Done=MoveTemp(F);return true;}
    virtual bool SendAssets(FGuid,FGuid,FCompletion F) override{++Gets;Done=MoveTemp(F);return true;}
    virtual void Cancel() override{Done={};}
    void Reply(int32 Code,const FString& Body=TEXT(""),double Delay=0){auto F=MoveTemp(Done);if(F)F({true,Code!=0,Code,Body,Delay});}
};
TSharedRef<FRPGAuctionCoordinator> Owner(TSharedRef<FFake> T,FRPGAuctionLobbyContext& C,double& Time,FRPGTradeBagSnapshot& B,const TCHAR* Name)
{auto O=MakeShared<FRPGAuctionCoordinator>(T,[&C]{return C;},Root(Name),[&Time]{return Time;});O->SetRegistrationBagSource([&B]{return B;},[&B]{B.Token=FGuid::NewGuid();return true;});return O;}
bool Review(TSharedRef<FRPGAuctionCoordinator> O,const FRPGTradeBagSnapshot& B)
{return O->ReviewRegistration(B.Token,B.Items[0].ItemId,TEXT("4"),LexToString(MAX_int64),TEXT("3600"));}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRegistrationPolicyTest,"ProjectRPG.Auction.Registration.Policy",AuctionRegistrationTests::Flags)
bool FAuctionRegistrationPolicyTest::RunTest(const FString&)
{
    using namespace AuctionRegistrationTests;FRPGAuctionRegistrationPolicy P;
    if(!TestTrue(TEXT("production policy decodes"),RPGAuction::DecodeRegistrationPolicy(Load(TEXT("policy")),P)))return false;
    TestEqual(TEXT("price remains max int64"),P.MaximumPrice,int64(MAX_int64));TestEqual(TEXT("fee preserves basis points"),P.FeeBasisPoints,333);
    auto Bad=Obj(Load(TEXT("policy")));Bad->SetStringField(TEXT("policyTag"),P.Tag.ToUpper());TestFalse(TEXT("policy tag strictly canonical"),RPGAuction::DecodeRegistrationPolicy(Json(Bad),P));
    auto C=Context();P=Policy();FRPGAuctionClient Client;Client.Bind(C.Endpoint,C.Account,C.Character);
    TestTrue(TEXT("legacy register retained"),Client.PrepareRegister(FGuid::NewGuid(),Id(TEXT("itemId")),1,4,MAX_int64,P.ObservedAt+FTimespan::FromHours(1)));Client.Reset();Client.Bind(C.Endpoint,C.Account,C.Character);
    TestTrue(TEXT("tagged register prepared"),Client.PrepareRegister(FGuid::NewGuid(),Id(TEXT("itemId")),1,4,MAX_int64,P.ObservedAt+FTimespan::FromHours(1),P.Tag));
    FRPGAuctionIntent I;Client.ExportPending(I);TestEqual(TEXT("original tag included"),Obj(I.Request.Body)->GetStringField(TEXT("expectedPolicyTag")),P.Tag);
    TestTrue(TEXT("tagged journal writes"),FRPGAuctionIntentJournal::Create(Root(TEXT("TaggedJournal")),I));FRPGAuctionIntent Disk;TestTrue(TEXT("tagged journal reloads"),FRPGAuctionIntentJournal::Load(Root(TEXT("TaggedJournal")),I.Scope,Disk)==ERPGAuctionJournalRead::Ready);
    FRPGAuctionClient Restored;Restored.Bind(C.Endpoint,C.Account,C.Character);TestTrue(TEXT("tag restored with original body"),Restored.RestorePending(Disk));FRPGTradeTicket Ticket;Restored.BeginAttempt(Ticket);
    TestTrue(TEXT("policy conflict after uncertain restore keeps intent"),Restored.CompleteAttempt(Ticket,409,TEXT("{\"error\":\"auction_policy_conflict\"}"))==ERPGAuctionResponse::Unknown);
    Client.BeginAttempt(Ticket);TestTrue(TEXT("first known policy refusal is definitive"),Client.CompleteAttempt(Ticket,409,TEXT("{\"error\":\"auction_policy_conflict\"}"))==ERPGAuctionResponse::Rejected);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRegistrationDraftTest,"ProjectRPG.Auction.Registration.Draft",AuctionRegistrationTests::Flags)
bool FAuctionRegistrationDraftTest::RunTest(const FString&)
{
    using namespace AuctionRegistrationTests;auto C=Context();double Time=100;auto B=Bag(C);auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,B,TEXT("RegistrationDraft"));
    O->GetScreenView();O->Tick();TestEqual(TEXT("open and tick no policy HTTP"),T->Gets,0);TestFalse(TEXT("policy required"),Review(O,B));
    O->ReadRegistrationPolicy();T->Reply(200,Load(TEXT("policy")));
    for(const FString& Bad:TArray<FString>{TEXT("0"),TEXT("01"),TEXT("-1"),TEXT("1.0"),TEXT("1e3"),TEXT("9223372036854775808")})TestFalse(TEXT("invalid prices rejected"),O->ReviewRegistration(B.Token,B.Items[0].ItemId,TEXT("4"),Bad,TEXT("3600")));
    TestFalse(TEXT("over-stack rejected"),O->ReviewRegistration(B.Token,B.Items[0].ItemId,TEXT("11"),TEXT("699"),TEXT("3600")));
    if(!TestTrue(TEXT("explicit valid review"),Review(O,B)))return false;const auto V=O->GetScreenView();TestNull(TEXT("review creates no nonce intent"),O->GetClient().GetPending());TestEqual(TEXT("review sends no POST"),T->Posts,0);
    TestTrue(TEXT("review contains exact price"),V.ReviewText.ToString().Contains(FText::AsNumber(MAX_int64).ToString()));TestTrue(TEXT("fee displayed exactly without monetary float"),V.ReviewText.ToString().Contains(TEXT("3.33%")));
    T->BeforeSend=[this](const FRPGAuctionIntent& I){FRPGAuctionIntent D;TestTrue(TEXT("durable before first POST"),FRPGAuctionIntentJournal::Load(Root(TEXT("RegistrationDraft")),I.Scope,D)==ERPGAuctionJournalRead::Ready);TestEqual(TEXT("same journal body"),D.Request.Body,I.Request.Body);};
    TestTrue(TEXT("final consent submits"),O->ConfirmListing(V.ReviewToken));TestFalse(TEXT("duplicate consent rejected"),O->ConfirmListing(V.ReviewToken));TestEqual(TEXT("single registration"),T->Posts,1);
    const auto Original=T->Last;TestEqual(TEXT("tag and price unchanged"),Obj(Original.Request.Body)->GetStringField(TEXT("expectedPolicyTag")),Policy().Tag);
    FString Receipt=Load(TEXT("registration"));const auto OldId=Obj(Receipt)->GetObjectField(TEXT("listing"))->GetStringField(TEXT("listingId"));Receipt=Receipt.Replace(*OldId,*Original.Request.ListingId.ToString(EGuidFormats::DigitsWithHyphensLower));auto Result=Obj(Receipt);Result->SetStringField(TEXT("requestId"),Original.Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
    T->Reply(200,Json(Result));TestNotNull(TEXT("matching registered result"),O->GetClient().GetReceipt());TestTrue(TEXT("refresh current assets before another registration"),O->GetClient().NeedsAssetRefresh());
    TestFalse(TEXT("no next registration before receipt assets imported"),Review(O,B));bool ProjectionCurrent=true;
    O->SetAssetSink([]{return true;},[](const FRPGTradeAssets&){return true;},[&ProjectionCurrent]{return ProjectionCurrent;});
    TestTrue(TEXT("read current registration assets"),O->ReloadAssets());auto Asset=Obj(Load(TEXT("registrationAssets")));Asset->SetStringField(TEXT("requestId"),Original.Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));T->Reply(200,Json(Asset));
    TestFalse(TEXT("current receipt assets acknowledged"),O->GetClient().NeedsAssetRefresh());
    ProjectionCurrent=false;B.Token=FGuid::NewGuid();B.Items[0].Revision=2;B.Items[0].Quantity=6;
    if(!TestTrue(TEXT("fresh bag after items read may supersede old projection ticket"),Review(O,B)))return false;
    TestTrue(TEXT("next registration uses newly read bag after previous assets acknowledged"),O->ConfirmListing(O->GetScreenView().ReviewToken));TestEqual(TEXT("two separately confirmed registrations"),T->Posts,2);T->Reply(0);O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRegistrationLifetimeTest,"ProjectRPG.Auction.Registration.Lifetime",AuctionRegistrationTests::Flags)
bool FAuctionRegistrationLifetimeTest::RunTest(const FString&)
{
    using namespace AuctionRegistrationTests;auto C=Context();double Time=100;auto B=Bag(C);auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,B,TEXT("RegistrationLifetime"));O->ReadRegistrationPolicy();T->Reply(200,Load(TEXT("policy")));
    Review(O,B);auto Token=O->GetScreenView().ReviewToken;B.Token=FGuid::NewGuid();TestFalse(TEXT("new bag generation invalidates review"),O->ConfirmListing(Token));
    Review(O,B);Token=O->GetScreenView().ReviewToken;++B.Items[0].Revision;TestFalse(TEXT("changed item revision invalidates review"),O->ConfirmListing(Token));
    Review(O,B);Token=O->GetScreenView().ReviewToken;Time=160;TestFalse(TEXT("expired policy cannot confirm"),O->ConfirmListing(Token));O->ReadRegistrationPolicy();T->Reply(200,Load(TEXT("policy")));
    B.bReady=false;TestFalse(TEXT("unready bag blocked"),Review(O,B));B.bReady=true;B.Items[0].bEligible=false;TestFalse(TEXT("ineligible item blocked"),Review(O,B));B.Items[0].bEligible=true;
    Review(O,B);Token=O->GetScreenView().ReviewToken;C.Character=FGuid::NewGuid();TestFalse(TEXT("selection changed before tick cannot submit"),O->ConfirmListing(Token));TestEqual(TEXT("all stale consents no POST"),T->Posts,0);O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionRegistrationWidgetTest,"ProjectRPG.Auction.Registration.Widget",AuctionRegistrationTests::Flags)
bool FAuctionRegistrationWidgetTest::RunTest(const FString&)
{
    using namespace AuctionRegistrationTests;auto C=Context();double Time=100;auto B=Bag(C);auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,B,TEXT("RegistrationWidget"));auto* W=NewObject<URPGAuctionWidget>();W->SetOwner(O);const auto Slate=W->TakeWidget();
    TestEqual(TEXT("widget open no HTTP"),T->Gets,0);FRPGAuctionRegistrationWidgetTestAccess::Policy(W);T->Reply(200,Load(TEXT("policy")));FRPGAuctionRegistrationWidgetTestAccess::Bag(W);FRPGAuctionRegistrationWidgetTestAccess::Select(W);FRPGAuctionRegistrationWidgetTestAccess::Inputs(W);FRPGAuctionRegistrationWidgetTestAccess::Review(W);
    if(!TestTrue(TEXT("actual form opens registration review"),O->GetScreenView().bHasReview))return false;FRPGAuctionRegistrationWidgetTestAccess::Edit(W);FRPGAuctionRegistrationWidgetTestAccess::Confirm(W);TestEqual(TEXT("editing price revokes consent"),T->Posts,0);
    FRPGAuctionRegistrationWidgetTestAccess::Inputs(W);FRPGAuctionRegistrationWidgetTestAccess::Review(W);const auto Closed=O->GetScreenView().ReviewToken;W->CloseScreen();TestFalse(TEXT("close invalidates draft consent"),O->ConfirmListing(Closed));
    auto* Again=NewObject<URPGAuctionWidget>();Again->SetOwner(O);const auto AgainSlate=Again->TakeWidget();FRPGAuctionRegistrationWidgetTestAccess::Select(Again);FRPGAuctionRegistrationWidgetTestAccess::Inputs(Again);FRPGAuctionRegistrationWidgetTestAccess::Review(Again);FRPGAuctionRegistrationWidgetTestAccess::Confirm(Again);T->Reply(0);
    const auto Body=T->Last.Request.Body;TestEqual(TEXT("actual final button created one request"),T->Posts,1);Again->CloseScreen();Time=101;O->Tick();TestNotNull(TEXT("closing preserves uncertain registration"),O->GetClient().GetPending());O->Retry();TestEqual(TEXT("retry preserves original tagged body"),T->Last.Request.Body,Body);T->Reply(409,TEXT("{\"error\":\"auction_policy_conflict\"}"));TestNotNull(TEXT("unknown registration remains recoverable after policy conflict"),O->GetClient().GetPending());O->Shutdown();return true;
}
#endif
