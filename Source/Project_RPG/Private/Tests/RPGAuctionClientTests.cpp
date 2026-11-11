#if WITH_DEV_AUTOMATION_TESTS
#include "Auction/RPGAuctionClient.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGAuctionTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Load(const TCHAR* Name){FString S;FFileHelper::LoadFileToString(S,*FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_WIRE_FIXTURE")),FString(Name)+TEXT(".json")));return S;}
TSharedPtr<FJsonObject> Obj(const FString& S){TSharedPtr<FJsonObject> O;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S),O);return O;}
FString Json(const TSharedPtr<FJsonObject>& O){FString S;FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&S));return S;}
FGuid Identity(const TCHAR* Name){FGuid G;FGuid::Parse(Obj(Load(TEXT("identity")))->GetStringField(Name),G);return G;}
FRPGAuctionClient Client(bool Buyer=false){FRPGAuctionClient C;C.Bind(TEXT("https://backend.example/"),TEXT("account"),Identity(Buyer?TEXT("buyerCharacterId"):TEXT("sellerCharacterId")));return C;}
FRPGAuctionListing Listing(bool Own=false,const TCHAR* Name=nullptr){FRPGAuctionListing L;RPGAuction::DecodeListing(Load(Name?Name:(Own?TEXT("own"):TEXT("listing"))),Own,L);return L;}
FRPGAuctionPayable Payable(const TCHAR* Name=TEXT("payable")){FRPGAuctionPayable P;RPGAuction::DecodePayable(Load(Name),P);return P;}
FString Receipt(const TCHAR* Name,const FRPGAuctionClient& C){auto O=Obj(Load(Name));if(!O||!C.GetPending())return {};O->SetStringField(TEXT("requestId"),Id(C.GetPending()->RequestId));return Json(O);}
bool Prepare(FRPGAuctionClient& C,ERPGAuctionAction A)
{
    switch(A){case ERPGAuctionAction::Register:{auto L=Listing(true);return C.PrepareRegister(L.ListingId,Identity(TEXT("itemId")),1,4,MAX_int64,L.ExpiresAt);}
        case ERPGAuctionAction::Purchase:return C.PreparePurchase(Listing());case ERPGAuctionAction::Cancel:return C.PrepareCancel(Listing(true,TEXT("cancelOwn")));
        case ERPGAuctionAction::Claim:return C.PrepareClaim(Payable(),200);default:return false;}
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionWireTest,"ProjectRPG.Auction.Client.Wire",RPGAuctionTests::Flags)
bool FRPGAuctionWireTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;
    FRPGAuctionListing L;if(!TestTrue(TEXT("actual backend listing"),RPGAuction::DecodeListing(Load(TEXT("listing")),false,L)))return false;
    TestEqual(TEXT("int64 max exact"),L.TotalPrice,MAX_int64);TestEqual(TEXT("seven fractional digits retained"),L.ExpiresAt.GetTicks()%ETimespan::TicksPerSecond,int64(7));
    TestEqual(TEXT("option value"),L.Item.Stats[0].Value,4.5);TestEqual(TEXT("quantity"),L.Quantity,4);
    FRPGAuctionPage Page;TestTrue(TEXT("actual backend query page"),RPGAuction::DecodePage(Load(TEXT("page")),false,Page));TestEqual(TEXT("bounded query page"),Page.Listings.Num(),1);TestFalse(TEXT("opaque cursor retained"),Page.NextCursor.IsEmpty());
    TestTrue(TEXT("owned sold projection"),RPGAuction::DecodeListing(Load(TEXT("sold")),true,L));TestTrue(TEXT("seller net is exact"),L.SellerProceeds.IsSet()&&L.SellerProceeds.GetValue()>9007199254740992LL);
    FRPGAuctionPayable P;TestTrue(TEXT("actual payable"),RPGAuction::DecodePayable(Load(TEXT("payable")),P));TestTrue(TEXT("payable above double range"),P.Remaining>9007199254740992LL);
    for(const auto& Bad:{TEXT("01"),TEXT("-1"),TEXT("+1"),TEXT("1.0"),TEXT("1e3"),TEXT(" 1"),TEXT("9223372036854775808"),TEXT("")})
    {auto O=Obj(Load(TEXT("listing")));O->SetStringField(TEXT("totalPrice"),Bad);FRPGAuctionListing Sentinel;Sentinel.TotalPrice=17;TestFalse(TEXT("noncanonical amount"),RPGAuction::DecodeListing(Json(O),false,Sentinel));TestEqual(TEXT("failure leaves output unchanged"),Sentinel.TotalPrice,int64(17));}
    auto O=Obj(Load(TEXT("listing")));O->SetNumberField(TEXT("revision"),1);TestFalse(TEXT("numeric revision rejected"),RPGAuction::DecodeListing(Json(O),false,L));
    TestFalse(TEXT("duplicate key rejected before collapse"),RPGAuction::DecodeListing(Load(TEXT("listing")).LeftChop(1)+TEXT(",\"totalPrice\":\"1\"}"),false,L));
    TestFalse(TEXT("owned listing is not public projection"),RPGAuction::DecodeListing(Load(TEXT("own")),false,L));
    O=Obj(Load(TEXT("listing")));O->SetStringField(TEXT("expiresAt"),TEXT("2026-11-12T01:00:00.12345678Z"));TestFalse(TEXT("subtick precision rejected"),RPGAuction::DecodeListing(Json(O),false,L));
    O->SetStringField(TEXT("expiresAt"),TEXT("2026-11-12T10:00:00.0000007+09:00"));TestTrue(TEXT("offset normalizes without rounding"),RPGAuction::DecodeListing(Json(O),false,L));TestEqual(TEXT("offset keeps original UTC"),L.ExpiresAt,Listing().ExpiresAt);
    O=Obj(Load(TEXT("page")));auto Array=O->GetArrayField(TEXT("listings"));auto Duplicate=Array[0];Array.Add(Duplicate);O->SetArrayField(TEXT("listings"),Array);TestFalse(TEXT("duplicate listing IDs rejected"),RPGAuction::DecodePage(Json(O),false,Page));
    O=Obj(Load(TEXT("page")));O->SetStringField(TEXT("nextCursor"),TEXT("../?account=other"));TestFalse(TEXT("cursor cannot become a path/query"),RPGAuction::DecodePage(Json(O),false,Page));
    O=Obj(Load(TEXT("payable")));O->SetStringField(TEXT("remainingAmount"),TEXT("9223372036854775807"));TestFalse(TEXT("remaining cannot exceed initial"),RPGAuction::DecodePayable(Json(O),P));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionCommandsTest,"ProjectRPG.Auction.Client.Commands",RPGAuctionTests::Flags)
bool FRPGAuctionCommandsTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;
    const ERPGAuctionAction Actions[]={ERPGAuctionAction::Register,ERPGAuctionAction::Purchase,ERPGAuctionAction::Cancel,ERPGAuctionAction::Claim};
    for(auto A:Actions){auto C=Client(A==ERPGAuctionAction::Purchase);if(!TestTrue(TEXT("valid command prepared"),Prepare(C,A)))return false;auto R=*C.GetPending();TestTrue(TEXT("new request nonce"),R.RequestId.IsValid());
        TestTrue(TEXT("character route"),R.RelativePath.StartsWith(TEXT("/api/characters/")));TestTrue(TEXT("auction namespace"),R.RelativePath.Contains(TEXT("/auctions/")));TestFalse(TEXT("no worker command"),R.RelativePath.EndsWith(TEXT("/expire")));
        FRPGAuctionIntent I;TestTrue(TEXT("validated export"),C.ExportPending(I));TestTrue(TEXT("immutable identity-scoped intent"),I.IsValid());TestFalse(TEXT("pending blocks another command"),Prepare(C,A));
        TestFalse(TEXT("no bearer material"),R.Body.Contains(TEXT("Authorization"))||R.Body.Contains(TEXT("account"))||R.Body.Contains(TEXT("seller"))||R.Body.Contains(TEXT("fee")));
        if(A==ERPGAuctionAction::Register||A==ERPGAuctionAction::Purchase)TestEqual(TEXT("wire int64 string"),Obj(R.Body)->GetStringField(TEXT("totalPrice")),FString(TEXT("9223372036854775807")));
    }
    auto C=Client();auto L=Listing();L.Revision=MAX_int64;TestFalse(TEXT("exhausted revision"),C.PreparePurchase(L));TestFalse(TEXT("zero quantity"),C.PrepareRegister(L.ListingId,Identity(TEXT("itemId")),1,0,1,L.ExpiresAt));
    TestFalse(TEXT("zero claim request"),C.PrepareClaim(Payable(),0));TestFalse(TEXT("cancel requires selected owning character"),Client(true).PrepareCancel(Listing(true,TEXT("cancelOwn"))));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionReceiptsTest,"ProjectRPG.Auction.Client.Receipts",RPGAuctionTests::Flags)
bool FRPGAuctionReceiptsTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;
    const ERPGAuctionAction Actions[]={ERPGAuctionAction::Register,ERPGAuctionAction::Purchase,ERPGAuctionAction::Cancel,ERPGAuctionAction::Claim};
    const TCHAR* Names[]={TEXT("registration"),TEXT("purchase"),TEXT("cancel"),TEXT("claim")};
    for(int32 N=0;N<4;++N){auto C=Client(N==1);if(!TestTrue(TEXT("prepare"),Prepare(C,Actions[N])))return false;const FString Body=Receipt(Names[N],C);FRPGTradeTicket T;TestTrue(TEXT("begin"),C.BeginAttempt(T));
        if(!TestTrue(TEXT("production mapper receipt accepted"),C.CompleteAttempt(T,200,Body)==ERPGAuctionResponse::Succeeded))return false;TestNull(TEXT("only receipt clears request"),C.GetPending());
        TestTrue(TEXT("success requests fresh assets, never delta application"),C.NeedsAssetRefresh());TestFalse(TEXT("wrong refresh ack ignored"),C.AcknowledgeAssets(FGuid::NewGuid()));
        TestTrue(TEXT("correct fresh-assets ack"),C.AcknowledgeAssets(C.GetReceipt()->RequestId));TestFalse(TEXT("refresh flag cleared"),C.NeedsAssetRefresh());TestTrue(TEXT("duplicate callback ignored"),C.CompleteAttempt(T,200,Body)==ERPGAuctionResponse::Ignored);}
    auto C=Client();if(!TestTrue(TEXT("prepare zero claim"),C.PrepareClaim(Payable(TEXT("payableAfter")),1)))return false;const auto Zero=Receipt(TEXT("claimZero"),C);FRPGTradeTicket T;C.BeginAttempt(T);
    if(!TestTrue(TEXT("zero claim is committed success"),C.CompleteAttempt(T,200,Zero)==ERPGAuctionResponse::Succeeded))return false;TestEqual(TEXT("zero paid"),C.GetReceipt()->Paid,int64(0));TestEqual(TEXT("zero claim keeps payable revision"),C.GetReceipt()->Revision,int64(2));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionRecoveryTest,"ProjectRPG.Auction.Client.UncertainRecovery",RPGAuctionTests::Flags)
bool FRPGAuctionRecoveryTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;
    for(int32 Status:{0,200,401,429,500,503}){auto C=Client(true);if(!TestTrue(TEXT("prepare uncertain purchase"),Prepare(C,ERPGAuctionAction::Purchase)))return false;auto Original=*C.GetPending();const auto Good=Receipt(TEXT("purchase"),C);FRPGTradeTicket T;C.BeginAttempt(T);
        TestTrue(TEXT("uncertain response retains intent"),C.CompleteAttempt(T,Status,TEXT("{}"))==ERPGAuctionResponse::Unknown);TestEqual(TEXT("request bytes unchanged"),C.GetPending()->Body,Original.Body);
        FRPGTradeTicket Read;FString Path;TestTrue(TEXT("read own request receipt"),C.BeginRecovery(Read,Path));TestTrue(TEXT("recovery targets original nonce"),Path.EndsWith(TEXT("/requests/")+Id(Original.RequestId)));
        TestFalse(TEXT("recovery blocks competing write attempt"),C.BeginAttempt(T));TestTrue(TEXT("404 is not non-commit proof"),C.CompleteRecovery(Read,404,TEXT("{\"error\":\"auction_not_found\"}"))==ERPGAuctionResponse::Unknown);
        C.BeginAttempt(T);TestTrue(TEXT("later conflict cannot erase old unknown"),C.CompleteAttempt(T,409,TEXT("{\"error\":\"auction_revision_conflict\"}"))==ERPGAuctionResponse::Unknown);
        TestEqual(TEXT("same nonce on retry"),C.GetPending()->RequestId,Original.RequestId);C.BeginRecovery(Read,Path);TestTrue(TEXT("receipt read resolves original write"),C.CompleteRecovery(Read,200,Good)==ERPGAuctionResponse::Succeeded);}
    auto C=Client(true);Prepare(C,ERPGAuctionAction::Purchase);FRPGTradeTicket T;C.BeginAttempt(T);TestTrue(TEXT("definitive first refusal"),C.CompleteAttempt(T,409,TEXT("{\"error\":\"auction_insufficient_balance\"}"))==ERPGAuctionResponse::Rejected);TestNull(TEXT("first refusal can release command"),C.GetPending());
    C=Client(true);Prepare(C,ERPGAuctionAction::Purchase);C.BeginAttempt(T);TestTrue(TEXT("nonce conflict always ambiguous"),C.CompleteAttempt(T,409,TEXT("{\"error\":\"auction_idempotency_conflict\"}"))==ERPGAuctionResponse::Unknown);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionLifetimeTest,"ProjectRPG.Auction.Client.Lifetime",RPGAuctionTests::Flags)
bool FRPGAuctionLifetimeTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;FRPGAuctionClient C;
    for(int32 Boundary=0;Boundary<3;++Boundary){C=Client(true);Prepare(C,ERPGAuctionAction::Purchase);const auto Good=Receipt(TEXT("purchase"),C);FRPGTradeTicket Old;C.BeginAttempt(Old);
        C.Bind(Boundary==0?TEXT("https://other.example"):TEXT("https://backend.example"),Boundary==1?TEXT("other"):TEXT("account"),Identity(Boundary==2?TEXT("sellerCharacterId"):TEXT("buyerCharacterId")));
        TestTrue(TEXT("identity boundary ignores stale callback"),C.CompleteAttempt(Old,200,Good)==ERPGAuctionResponse::Ignored);TestNull(TEXT("old result cannot enter new identity"),C.GetReceipt());}
    C=Client(true);Prepare(C,ERPGAuctionAction::Purchase);const auto Proof=Receipt(TEXT("purchase"),C);FRPGTradeTicket T;C.BeginAttempt(T);auto Wrong=Obj(Proof);Wrong->SetStringField(TEXT("requestId"),Id(FGuid::NewGuid()));
    TestTrue(TEXT("wrong nonce remains unknown"),C.CompleteAttempt(T,200,Json(Wrong))==ERPGAuctionResponse::Unknown);FRPGTradeTicket New;C.BeginAttempt(New);TestTrue(TEXT("old attempt ticket ignored"),C.CompleteAttempt(T,200,Proof)==ERPGAuctionResponse::Ignored);
    Wrong=Obj(Proof);Wrong->GetObjectField(TEXT("listing"))->SetStringField(TEXT("revision"),TEXT("3"));TestTrue(TEXT("wrong listing revision not accepted"),C.CompleteAttempt(New,200,Json(Wrong))==ERPGAuctionResponse::Unknown);
    C.BeginAttempt(New);Wrong=Obj(Proof);Wrong->SetArrayField(TEXT("items"),{});TestTrue(TEXT("purchase cannot prove delivery with empty item projection"),C.CompleteAttempt(New,200,Json(Wrong))==ERPGAuctionResponse::Unknown);
    C.BeginAttempt(New);TestTrue(TEXT("matching proof still succeeds"),C.CompleteAttempt(New,200,Proof)==ERPGAuctionResponse::Succeeded);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAuctionIntentTest,"ProjectRPG.Auction.Client.Intent",RPGAuctionTests::Flags)
bool FRPGAuctionIntentTest::RunTest(const FString&)
{
    using namespace RPGAuctionTests;auto C=Client(true);Prepare(C,ERPGAuctionAction::Purchase);FRPGAuctionIntent I;if(!TestTrue(TEXT("export prepared command"),C.ExportPending(I)))return false;
    auto Restored=Client(true);if(!TestTrue(TEXT("same identity restore"),Restored.RestorePending(I)))return false;TestTrue(TEXT("restored always unknown"),Restored.GetPending()->Delivery==ERPGTradeDelivery::Unknown);
    TestEqual(TEXT("original bytes retained"),Restored.GetPending()->Body,I.Request.Body);TestFalse(TEXT("restore cannot replace pending"),Restored.RestorePending(I));
    for(int32 N=0;N<3;++N){auto Wrong=I;if(N==0)Wrong.Scope.Endpoint=TEXT("https://other.example/api");else if(N==1)Wrong.Scope.Account=TEXT("other");else Wrong.Scope.Character=Identity(TEXT("sellerCharacterId"));auto Other=Client(true);TestFalse(TEXT("other scope cannot restore"),Other.RestorePending(Wrong));}
    auto Bad=I;Bad.Request.RelativePath+=TEXT("?owner=other");TestFalse(TEXT("path must match action and identity"),Bad.IsValid());Bad=I;Bad.Request.Action=ERPGAuctionAction::Cancel;TestFalse(TEXT("wrong action shape"),Bad.IsValid());
    Bad=I;auto B=Obj(Bad.Request.Body);B->SetStringField(TEXT("authorization"),TEXT("secret"));Bad.Request.Body=Json(B);TestFalse(TEXT("intent has no credentials/unknown fields"),Bad.IsValid());
    auto Register=Client();Prepare(Register,ERPGAuctionAction::Register);FRPGAuctionIntent Fraction;Register.ExportPending(Fraction);Fraction.Request.Body=Fraction.Request.Body.Replace(TEXT("\"quantity\":4"),TEXT("\"quantity\":4.0"));TestFalse(TEXT("restored quantity uses integer JSON tokens"),Fraction.IsValid());
    FRPGTradeTicket T;Restored.BeginAttempt(T);TestTrue(TEXT("restored command cannot be cleared by first observed 4xx"),Restored.CompleteAttempt(T,404,TEXT("{\"error\":\"auction_not_found\"}"))==ERPGAuctionResponse::Unknown);
    FString Path;Restored.BeginRecovery(T,Path);TestTrue(TEXT("restore resolves only receipt"),Restored.CompleteRecovery(T,200,Receipt(TEXT("purchase"),Restored))==ERPGAuctionResponse::Succeeded);return true;
}
#endif
