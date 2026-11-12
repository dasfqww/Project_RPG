#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Lobby/RPGAuctionWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"

struct FRPGAuctionWidgetTestAccess
{
    static void Mine(URPGAuctionWidget* W){W->MineButton->OnClicked.Broadcast();}
    static void Select(URPGAuctionWidget* W,int32 Row){W->Rows->SetSelectedIndex(Row);}
    static bool DetailEnabled(URPGAuctionWidget* W){return W->DetailButton->GetIsEnabled();}
    static void Detail(URPGAuctionWidget* W){W->DetailButton->OnClicked.Broadcast();}
    static void Recover(URPGAuctionWidget* W){W->RecoverButton->OnClicked.Broadcast();}
    static void Retry(URPGAuctionWidget* W){W->RetryButton->OnClicked.Broadcast();}
    static void Assets(URPGAuctionWidget* W){W->AssetsButton->OnClicked.Broadcast();}
    static int32 Rows(URPGAuctionWidget* W){return W->Rows->GetOptionCount();}
    static FString AssetText(URPGAuctionWidget* W){return W->AssetText->GetText().ToString();}
};
namespace AuctionScreenTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FString Load(const TCHAR* Name){FString S;FFileHelper::LoadFileToString(S,*(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_WIRE_FIXTURE"))/(FString(Name)+TEXT(".json"))));return S;}
TSharedPtr<FJsonObject> Obj(const FString& S){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O,FJsonSerializer::EFlags::StoreNumbersAsStrings);return O;}
FString Json(const TSharedPtr<FJsonObject>& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
FRPGAuctionListing Listing(){FRPGAuctionListing L;RPGAuction::DecodeListing(Load(TEXT("listing")),false,L);return L;}
FGuid Character(){FGuid G;FGuid::Parse(Obj(Load(TEXT("identity")))->GetStringField(TEXT("buyerCharacterId")),G);return G;}
FRPGAuctionLobbyContext Context(){return {TEXT("https://backend.example/api"),TEXT("76561198880000002"),Character(),true,true,true,true};}
FString Root(const TCHAR* Name){FString R;FParse::Value(FCommandLine::Get(),TEXT("AuctionJournalTestRoot="),R);return R.IsEmpty()?FString():R/Name;}
FString Page(const FString& Cursor=TEXT("cursor_1")){return TEXT("{\"listings\":[")+Load(TEXT("listing"))+TEXT("],\"nextCursor\":")+(Cursor.IsEmpty()?TEXT("null"):TEXT("\"")+Cursor+TEXT("\""))+TEXT("}");}
FString WithRequest(const TCHAR* Name,FGuid Id){auto O=Obj(Load(Name));O->SetStringField(TEXT("requestId"),Id.ToString(EGuidFormats::DigitsWithHyphensLower));return Json(O);}
struct FFake final : IRPGAuctionTransport
{
    FRPGAuctionLobbyContext* C;FCompletion Done;FRPGAuctionIntent Last;FRPGAuctionSearch Query;FGuid LastId;
    int32 Posts=0,Pages=0,Details=0,Receipts=0,Assets=0;bool Mine=false;
    explicit FFake(FRPGAuctionLobbyContext& In):C(&In){}
    virtual bool GetContext(FString& E,FString& A) const override {E=C->Endpoint;A=C->Account;return !A.IsEmpty();}
    virtual bool CanSend() const override {return C->bAuthenticated && !Done;}
    virtual bool SendIntent(const FRPGAuctionIntent& I,FCompletion F) override {++Posts;Last=I;Done=MoveTemp(F);return true;}
    virtual bool SendPage(FGuid,const FRPGAuctionSearch& Q,bool M,FCompletion F) override {++Pages;Query=Q;Mine=M;Done=MoveTemp(F);return true;}
    virtual bool SendListing(FGuid,FGuid Id,FCompletion F) override {++Details;LastId=Id;Done=MoveTemp(F);return true;}
    virtual bool SendReceipt(FGuid,FGuid Id,FCompletion F) override {++Receipts;LastId=Id;Done=MoveTemp(F);return true;}
    virtual bool SendPayable(FGuid,FGuid,FCompletion) override {return false;}
    virtual bool SendAssets(FGuid,FGuid Id,FCompletion F) override {++Assets;LastId=Id;Done=MoveTemp(F);return true;}
    virtual void Cancel() override {Done={};}
    void Reply(int32 Code,const FString& Body=TEXT(""),double Delay=0)
    {auto F=MoveTemp(Done);if(F)F({true,Code!=0,Code,Body,Delay});}
};
TSharedRef<FRPGAuctionCoordinator> Owner(TSharedRef<FFake> T,FRPGAuctionLobbyContext& C,double& Time,const TCHAR* Name)
{return MakeShared<FRPGAuctionCoordinator>(T,[&C]{return C;},Root(Name),[&Time]{return Time;});}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionScreenBrowse,"ProjectRPG.Auction.Screen.Browse",AuctionScreenTests::Flags)
bool FAuctionScreenBrowse::RunTest(const FString&)
{
    using namespace AuctionScreenTests;auto C=Context();double Time=100;auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,TEXT("ScreenBrowse"));
    O->SetItemPresenter([](const FRPGTradeItemDetails&,FText& N,FText& D){N=FText::FromString(TEXT("검사용 광석"));D=FText::FromString(TEXT("확인한 옵션"));return true;});
    O->GetScreenView();O->Tick();TestEqual(TEXT("opening view never sends"),T->Pages+T->Posts,0);
    TestTrue(TEXT("explicit public read"),O->ReadPage(false));TestFalse(TEXT("single flight"),O->ReadPage(true));T->Reply(200,Page());
    const auto V=O->GetScreenView();if(!TestEqual(TEXT("one row"),V.Rows.Num(),1))return false;
    TestTrue(TEXT("human name"),V.Rows[0].Label.ToString().Contains(TEXT("검사용 광석")));
    TestFalse(TEXT("no internal tag/id/definition name"),V.Rows[0].Label.ToString().Contains(TEXT("Item.Ore")) || V.Rows[0].Label.ToString().Contains(Listing().ListingId.ToString()));
    TestFalse(TEXT("stale page token refused"),O->ReadDetail(FGuid::NewGuid(),Listing().ListingId));
    TestFalse(TEXT("unlisted id refused"),O->ReadDetail(V.PageToken,FGuid::NewGuid()));
    TestTrue(TEXT("explicit selected detail"),O->ReadDetail(V.PageToken,V.Rows[0].ListingId));T->Reply(200,Load(TEXT("listing")));
    TestTrue(TEXT("detail contains presentation"),O->GetScreenView().Detail.ToString().Contains(TEXT("확인한 옵션")));
    TestTrue(TEXT("next uses captured cursor"),O->NextPage(V.PageToken));TestEqual(TEXT("opaque cursor preserved"),T->Query.Cursor,FString(TEXT("cursor_1")));T->Reply(200,Page(TEXT("")));
    TestFalse(TEXT("old page selection invalidated"),O->ReadDetail(V.PageToken,Listing().ListingId));TestFalse(TEXT("no next on last page"),O->GetScreenView().bCanNext);
    TestEqual(TEXT("browsing never creates transaction"),T->Posts,0);O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionScreenContext,"ProjectRPG.Auction.Screen.ContextAndThrottle",AuctionScreenTests::Flags)
bool FAuctionScreenContext::RunTest(const FString&)
{
    using namespace AuctionScreenTests;auto C=Context();double Time=100;auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,TEXT("ScreenContext"));
    C.bEnabled=false;TestFalse(TEXT("disabled public read blocked"),O->ReadPage(false));TestTrue(TEXT("own recovery history remains"),O->ReadPage(true));T->Reply(200,TEXT("{\"listings\":[],\"nextCursor\":null}"));
    TestTrue(TEXT("empty history view"),O->GetScreenView().Rows.IsEmpty());TestTrue(TEXT("explicit own refresh"),O->ReadPage(true));T->Reply(429,TEXT("{}"),30);
    Time=129;O->Tick();TestFalse(TEXT("server deadline reflected in controls"),O->GetScreenView().bCanMine);TestFalse(TEXT("read cannot bypass delay"),O->ReadPage(true));
    Time=1;TestFalse(TEXT("clock rollback"),O->ReadPage(true));Time=130;O->Tick();TestTrue(TEXT("deadline release"),O->GetScreenView().bCanMine);
    C.bEnabled=true;O->ReadPage(false);auto Late=T->Done;C.Character=FGuid::NewGuid();O->Tick();Late({true,true,200,Page(),0});
    TestTrue(TEXT("old character page hidden"),O->GetScreenView().Rows.IsEmpty());O->ReadPage(false);T->Reply(200,TEXT("{\"listings\":[],\"listings\":[],\"nextCursor\":null}"));
    TestTrue(TEXT("malformed success never populates"),O->GetScreenView().Rows.IsEmpty());
    C.bAuthenticated=false;TestFalse(TEXT("expired auth cannot read"),O->GetScreenView().bCanPublic);O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionScreenWidget,"ProjectRPG.Auction.Screen.WidgetAndRecovery",AuctionScreenTests::Flags)
bool FAuctionScreenWidget::RunTest(const FString&)
{
    using namespace AuctionScreenTests;auto C=Context();double Time=100;auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,TEXT("ScreenWidget"));
    auto* W=NewObject<URPGAuctionWidget>();W->SetOwner(O);const auto Slate=W->TakeWidget();
    TestTrue(TEXT("actual widget built"),Slate->GetVisibility().IsVisible());TestEqual(TEXT("open never sends"),T->Pages+T->Posts+T->Receipts,0);
    FRPGAuctionWidgetTestAccess::Mine(W);TestTrue(TEXT("own tab dispatch"),T->Mine);T->Reply(200,TEXT("{\"listings\":[],\"nextCursor\":null}"));
    TestFalse(TEXT("no implicit selection"),FRPGAuctionWidgetTestAccess::DetailEnabled(W));
    TestTrue(TEXT("public page"),O->ReadPage(false));T->Reply(200,Page());TestEqual(TEXT("real combo row"),FRPGAuctionWidgetTestAccess::Rows(W),1);
    FRPGAuctionWidgetTestAccess::Select(W,0);TestTrue(TEXT("explicit selection enables detail"),FRPGAuctionWidgetTestAccess::DetailEnabled(W));
    FRPGAuctionWidgetTestAccess::Detail(W);T->Reply(200,Load(TEXT("listing")));
    int32 Closed=0;W->OnClosed.AddLambda([&Closed]{++Closed;});W->CloseScreen();W->CloseScreen();TestEqual(TEXT("close once"),Closed,1);
    const int32 Before=T->Pages;FRPGAuctionWidgetTestAccess::Mine(W);TestEqual(TEXT("queued closed callback ignored"),T->Pages,Before);
    auto* Again=NewObject<URPGAuctionWidget>();Again->SetOwner(O);const auto AgainSlate=Again->TakeWidget();
    TestEqual(TEXT("cached page survives view recreation"),FRPGAuctionWidgetTestAccess::Rows(Again),1);
    auto OtherContext=C;OtherContext.Account=TEXT("76561198880000003");OtherContext.Character=FGuid::NewGuid();
    auto OtherTransport=MakeShared<FFake>(OtherContext);auto Other=Owner(OtherTransport,OtherContext,Time,TEXT("ScreenRetarget"));
    Again->SetOwner(Other);TestEqual(TEXT("retarget to empty owner immediately clears old options"),FRPGAuctionWidgetTestAccess::Rows(Again),0);
    Again->SetOwner(O);TestEqual(TEXT("retarget restores only current owner page"),FRPGAuctionWidgetTestAccess::Rows(Again),1);Other->Shutdown();
    if(!TestTrue(TEXT("original purchase prepared by owner"),O->Purchase(Listing())))return false;
    const auto Original=T->Last;Again->CloseScreen();T->Reply(0);Time=101;O->Tick();
    TestNotNull(TEXT("close preserves uncertain intent"),O->GetClient().GetPending());
    auto* Recovery=NewObject<URPGAuctionWidget>();Recovery->SetOwner(O);const auto RecoverySlate=Recovery->TakeWidget();
    TestEqual(TEXT("reopen never replays"),T->Posts,1);FRPGAuctionWidgetTestAccess::Recover(Recovery);TestEqual(TEXT("same original receipt id"),T->LastId,Original.Request.RequestId);
    T->Reply(404,TEXT("{\"error\":\"auction_not_found\"}"));TestNotNull(TEXT("404 keeps original"),O->GetClient().GetPending());Time=102;O->Tick();
    FRPGAuctionWidgetTestAccess::Retry(Recovery);TestEqual(TEXT("original body replay"),T->Last.Request.Body,Original.Request.Body);TestEqual(TEXT("only explicit replay"),T->Posts,2);
    T->Reply(200,WithRequest(TEXT("purchase"),Original.Request.RequestId));
    O->SetAssetSink([]{return true;},[](const FRPGTradeAssets&){return true;},[]{return true;});
    FRPGAuctionWidgetTestAccess::Assets(Recovery);T->Reply(200,WithRequest(TEXT("assets"),Original.Request.RequestId));
    TestFalse(TEXT("current asset read acknowledged"),O->GetClient().NeedsAssetRefresh());TestFalse(TEXT("asset status visible"),FRPGAuctionWidgetTestAccess::AssetText(Recovery).IsEmpty());
    TestFalse(TEXT("resolved owner releases transitions"),O->BlocksTransitions());
    const auto OriginalCharacter=C.Character;const auto OtherCharacter=FGuid::NewGuid();
    FRPGAuctionClient Foreign;Foreign.Bind(C.Endpoint,C.Account,OtherCharacter);Foreign.PreparePurchase(Listing());FRPGAuctionIntent ForeignIntent;Foreign.ExportPending(ForeignIntent);
    if(!TestTrue(TEXT("other character original intent saved"),FRPGAuctionIntentJournal::Create(Root(TEXT("ScreenWidget")),ForeignIntent)))return false;
    int32 ContextClosed=0;Recovery->OnClosed.AddLambda([&ContextClosed]{++ContextClosed;});
    const int32 PostsBefore=T->Posts,PagesBefore=T->Pages;C.Character=OtherCharacter;
    FRPGAuctionWidgetTestAccess::Retry(Recovery);FRPGAuctionWidgetTestAccess::Mine(Recovery);
    TestEqual(TEXT("context change closes old view before next tick"),ContextClosed,1);
    TestEqual(TEXT("stale click cannot retry other character intent"),T->Posts,PostsBefore);
    TestEqual(TEXT("stale click cannot read other character history"),T->Pages,PagesBefore);
    FRPGAuctionIntent StillSaved;TestTrue(TEXT("other character original remains on disk"),FRPGAuctionIntentJournal::Load(Root(TEXT("ScreenWidget")),ForeignIntent.Scope,StillSaved)==ERPGAuctionJournalRead::Ready);
    C.Character=OriginalCharacter;O->Tick();TestNull(TEXT("original character has no foreign pending"),O->GetClient().GetPending());
    O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionScreenStorage,"ProjectRPG.Auction.Screen.StorageRecheck",AuctionScreenTests::Flags)
bool FAuctionScreenStorage::RunTest(const FString&)
{
    using namespace AuctionScreenTests;auto C=Context();double Time=100;const auto Directory=Root(TEXT("ScreenStorage"));
    FRPGAuctionClient Client;Client.Bind(C.Endpoint,C.Account,C.Character);Client.PreparePurchase(Listing());FRPGAuctionIntent I;Client.ExportPending(I);
    if(!TestTrue(TEXT("persist fixture"),FRPGAuctionIntentJournal::Create(Directory,I)))return false;FString Key;FRPGAuctionIntentJournal::ScopeKey(I.Scope,Key);
    TUniquePtr<IFileHandle> Lock(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(Directory/(Key+TEXT(".lock"))),false,false));
    auto T=MakeShared<FFake>(C);auto O=Owner(T,C,Time,TEXT("ScreenStorage"));TestTrue(TEXT("blocked storage exposed"),O->GetScreenView().bCanStorage);
    Lock.Reset();TestTrue(TEXT("recheck restores only"),O->RecheckStorage());TestNotNull(TEXT("original restored"),O->GetClient().GetPending());
    TestEqual(TEXT("storage repair never sends money command"),T->Posts+T->Receipts,0);TestTrue(TEXT("receipt control enabled after repair"),O->GetScreenView().bCanRecover);
    O->Shutdown();return true;
}
#endif
