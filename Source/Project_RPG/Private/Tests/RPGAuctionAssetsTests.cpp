#if WITH_DEV_AUTOMATION_TESTS
#include "Auction/RPGAuctionCoordinator.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace AuctionAssetsTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FString Account=TEXT("76561198880000002");
FString Load(const TCHAR* Name)
{FString S;FFileHelper::LoadFileToString(S,*(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_WIRE_FIXTURE"))/(FString(Name)+TEXT(".json"))));return S;}
TSharedPtr<FJsonObject> Obj(const FString& S)
{TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O,FJsonSerializer::EFlags::StoreNumbersAsStrings);return O;}
FString Json(const TSharedPtr<FJsonObject>& O)
{FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
FGuid Character(){FGuid Id;FGuid::Parse(Obj(Load(TEXT("identity")))->GetStringField(TEXT("buyerCharacterId")),Id);return Id;}
FString Root(const TCHAR* Name)
{FString R;FParse::Value(FCommandLine::Get(),TEXT("AuctionJournalTestRoot="),R);return R.IsEmpty()?FString():R/Name;}
FString WithRequest(const TCHAR* Name,FGuid Request)
{auto O=Obj(Load(Name));O->SetStringField(TEXT("requestId"),Request.ToString(EGuidFormats::DigitsWithHyphensLower));return Json(O);}
struct FFake final : IRPGAuctionTransport
{
    FCompletion Done;FRPGAuctionIntent Last;int32 Reads=0;
    virtual bool GetContext(FString& E,FString& A) const override {E=TEXT("https://backend.example/api");A=Account;return true;}
    virtual bool CanSend() const override {return !Done;}
    virtual bool SendIntent(const FRPGAuctionIntent& I,FCompletion C) override {Last=I;Done=MoveTemp(C);return true;}
    virtual bool SendAssets(FGuid,FGuid Request,FCompletion C) override {++Reads;Done=MoveTemp(C);return Request==Last.Request.RequestId;}
    virtual bool SendPage(FGuid,const FRPGAuctionSearch&,bool,FCompletion) override {return false;}
    virtual bool SendListing(FGuid,FGuid,FCompletion) override {return false;}
    virtual bool SendPayable(FGuid,FGuid,FCompletion) override {return false;}
    virtual bool SendReceipt(FGuid,FGuid,FCompletion) override {return false;}
    virtual void Cancel() override {Done={};}
    void Reply(int32 Code,const FString& Body,double Retry=0)
    {auto C=MoveTemp(Done);if(C)C({true,Code!=0,Code,Body,Retry});}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionAssetsCodec,"ProjectRPG.Auction.Assets.Codec",AuctionAssetsTests::Flags)
bool FAuctionAssetsCodec::RunTest(const FString&)
{
    using namespace AuctionAssetsTests;const auto Raw=Load(TEXT("assets"));FRPGTradeAssets A;
    if(!TestTrue(TEXT("actual repository/current mapper snapshot"),FRPGTradeAssets::DecodeOwned(Raw,Character(),Account,A)))return false;
    const int32 InitialInventoryCount=A.Items.Num();
    TestEqual(TEXT("buyer current wallet"),A.Wallet.Balances[0].Balance,int64(0));
    TestEqual(TEXT("current read preserves starter equipment too"),A.Items.Num()+A.Mail.Num(),Obj(Raw)->GetArrayField(TEXT("items")).Num());
    TestEqual(TEXT("received quantity"),A.Items[0].GetQuantity(),4);
    const auto Wide=Raw.Replace(TEXT("\"balance\":0"),TEXT("\"balance\":9223372036854775807"));
    TestTrue(TEXT("full int64 exact"),FRPGTradeAssets::DecodeOwned(Wide,Character(),Account,A));TestEqual(TEXT("no double rounding"),A.Wallet.Balances[0].Balance,MAX_int64);
    for(const auto& Token:{TEXT("0.5"),TEXT("1e2"),TEXT("9223372036854775808"),TEXT("\"0\"")})
        TestFalse(TEXT("invalid integer wire token"),FRPGTradeAssets::DecodeOwned(Raw.Replace(TEXT("\"balance\":0"),*(FString(TEXT("\"balance\":"))+Token)),Character(),Account,A));
    auto O=Obj(Raw);O->SetBoolField(TEXT("isComplete"),false);TestFalse(TEXT("truncated read rejected"),FRPGTradeAssets::DecodeOwned(Json(O),Character(),Account,A));
    TestFalse(TEXT("foreign account"),FRPGTradeAssets::DecodeOwned(Raw,Character(),TEXT("other"),A));
    TestFalse(TEXT("unknown item tag remains invalid"),FRPGTradeAssets::DecodeOwned(Raw.Replace(TEXT("Item.Ore"),TEXT("Item.UnknownFixture")),Character(),Account,A));
    TestFalse(TEXT("unknown stat tag remains invalid"),FRPGTradeAssets::DecodeOwned(Raw.Replace(TEXT("Stat.Value"),TEXT("Stat.UnknownFixture")),Character(),Account,A));
    TestFalse(TEXT("foreign character"),FRPGTradeAssets::DecodeOwned(Raw,FGuid::NewGuid(),Account,A));
    O=Obj(Raw);auto Rows=O->GetArrayField(TEXT("items"));auto Duplicate=Rows[0];Rows.Add(Duplicate);O->SetArrayField(TEXT("items"),Rows);
    TestFalse(TEXT("duplicate item rejected"),FRPGTradeAssets::DecodeOwned(Json(O),Character(),Account,A));
    while(Rows.Num()<501)Rows.Add(Duplicate);O->SetArrayField(TEXT("items"),Rows);TestFalse(TEXT("bounded owned read"),FRPGTradeAssets::DecodeOwned(Json(O),Character(),Account,A));
    O=Obj(Raw);auto Location=O->GetArrayField(TEXT("items"))[0]->AsObject()->GetObjectField(TEXT("location"));
    Location->SetStringField(TEXT("containerType"),TEXT("Mail"));Location->SetStringField(TEXT("containerId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));Location->SetNumberField(TEXT("slotIndex"),0);
    TestTrue(TEXT("mail projection separate from inventory"),FRPGTradeAssets::DecodeOwned(Json(O),Character(),Account,A));TestEqual(TEXT("mail one"),A.Mail.Num(),1);TestEqual(TEXT("remaining inventory preserved"),A.Items.Num(),InitialInventoryCount-1);
    FRPGTradeSession Completed;Completed.Status=ERPGTradeStatus::Completed;
    TestFalse(TEXT("trade envelope validation still required"),FRPGTradeAssets::Decode(Raw,Completed,Character(),Account,A));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionAssetsProjection,"ProjectRPG.Auction.Assets.Projection",AuctionAssetsTests::Flags)
bool FAuctionAssetsProjection::RunTest(const FString&)
{
    using namespace AuctionAssetsTests;double Time=100;FRPGAuctionLobbyContext C{TEXT("https://backend.example/api"),Account,Character(),true,true,true,true};
    auto T=MakeShared<FFake>();auto Owner=MakeShared<FRPGAuctionCoordinator>(T,[&C]{return C;},Root(TEXT("AssetsProjection")),[&Time]{return Time;});
    FRPGAuctionListing L;if(!TestTrue(TEXT("listing fixture"),RPGAuction::DecodeListing(Load(TEXT("listing")),false,L)))return false;
    if(!TestTrue(TEXT("durable purchase"),Owner->Purchase(L)))return false;
    const auto Request=T->Last.Request.RequestId;T->Reply(200,WithRequest(TEXT("purchase"),Request));
    bool Current=true,Accept=true;int32 Applies=0;int64 Balance=10;
    Owner->SetAssetSink([&]{Current=true;return true;},[&](const FRPGTradeAssets& A){++Applies;Balance=A.Wallet.Balances[0].Balance;return Accept;},[&]{return Current;});
    TestTrue(TEXT("fresh assets read"),Owner->ReloadAssets());TestTrue(TEXT("shared single flight blocks mutation"),Owner->IsBusy());
    T->Reply(200,WithRequest(TEXT("assets"),FGuid::NewGuid()));TestEqual(TEXT("wrong request never imports"),Applies,0);TestTrue(TEXT("refresh still needed"),Owner->GetClient().NeedsAssetRefresh());
    TestTrue(TEXT("retry assets read"),Owner->ReloadAssets());T->Reply(200,WithRequest(TEXT("assets"),Request));
    TestEqual(TEXT("replace wallet, no receipt delta"),Balance,int64(0));TestFalse(TEXT("only current projection releases refresh"),Owner->GetClient().NeedsAssetRefresh());
    TestNotNull(TEXT("current assets available"),Owner->GetAssets());
    C.Character=FGuid::NewGuid();TestNull(TEXT("getter rejects changed selection before ticker"),Owner->GetAssets());C.Character=Character();
    TestTrue(TEXT("explicit reread"),Owner->ReloadAssets());T->Reply(200,WithRequest(TEXT("assets"),Request));TestEqual(TEXT("reread never adds old assets"),Balance,int64(0));
    Current=false;TestNull(TEXT("newer projection invalidates cache"),Owner->GetAssets());TestFalse(TEXT("new command cannot use obsolete projection"),Owner->Purchase(L));
    TestTrue(TEXT("reacquire projection"),Owner->ReloadAssets());Accept=false;T->Reply(200,WithRequest(TEXT("assets"),Request));TestNull(TEXT("failed apply not presented"),Owner->GetAssets());
    TestFalse(TEXT("failed read cannot authorize another command"),Owner->Purchase(L));
    Owner->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionAssetsLifetime,"ProjectRPG.Auction.Assets.Lifetime",AuctionAssetsTests::Flags)
bool FAuctionAssetsLifetime::RunTest(const FString&)
{
    using namespace AuctionAssetsTests;double Time=100;FRPGAuctionLobbyContext C{TEXT("https://backend.example/api"),Account,Character(),true,true,true,true};
    auto T=MakeShared<FFake>();auto O=MakeShared<FRPGAuctionCoordinator>(T,[&C]{return C;},Root(TEXT("AssetsLifetime")),[&Time]{return Time;});
    FRPGAuctionListing L;RPGAuction::DecodeListing(Load(TEXT("listing")),false,L);if(!TestTrue(TEXT("purchase"),O->Purchase(L)))return false;
    const auto Request=T->Last.Request.RequestId;T->Reply(200,WithRequest(TEXT("purchase"),Request));int32 Applied=0;
    bool ProjectionCurrent=true;
    O->SetAssetSink([&]{ProjectionCurrent=true;return true;},[&](const FRPGTradeAssets&){++Applied;ProjectionCurrent=false;return true;},[&]{return ProjectionCurrent;});
    O->ReloadAssets();T->Reply(429,TEXT("{}"),30);Time=129;TestFalse(TEXT("assets respects shared Retry-After"),O->ReloadAssets());Time=130;
    TestTrue(TEXT("after throttle"),O->ReloadAssets());T->Reply(200,WithRequest(TEXT("assets"),Request));TestTrue(TEXT("reentrant/newer projection leaves refresh pending"),O->GetClient().NeedsAssetRefresh());
    TestTrue(TEXT("another read"),O->ReloadAssets());auto Late=T->Done;const int32 Before=Applied;C.Character=FGuid::NewGuid();O->Tick();Late({true,true,200,WithRequest(TEXT("assets"),Request),0});
    TestEqual(TEXT("old character never receives old assets"),Applied,Before);TestNull(TEXT("stale cache empty"),O->GetAssets());
    O->Shutdown();return true;
}
#endif
