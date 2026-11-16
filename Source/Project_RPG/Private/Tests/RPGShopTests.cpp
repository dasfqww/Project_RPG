#if WITH_DEV_AUTOMATION_TESTS
#include "Shop/RPGShopCoordinator.h"
#include "Shop/RPGShopJournal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace RPGShopTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid Character(11,22,33,44),Item(55,66,77,88);
const FString Account=TEXT("76561198000000000");
FString Id(FGuid V){return V.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Snapshot()
{
    return FString::Printf(TEXT(R"({"shopId":"supplies","displayName":"보급 상인","policyTag":"v1","currencyCode":"Gold","balance":"1000","availableBalance":"900","inventoryFreeSlots":[80,79,79],"offers":[{"offerId":"potion","displayName":"회복약","definitionType":"Item","definitionName":"GameItem.Consume.Potion","definitionVersion":1,"unitPrice":"50","maximumQuantity":10,"maximumStackSize":99,"maximumPurchasable":10}],"saleItems":[{"itemId":"%s","revision":"9007199254740993","ruleId":"junk","displayName":"부서진 부품","quantity":4,"maximumQuantity":4,"unitPrice":"10","canSell":true,"reason":""}]})"),*Id(Item));
}
FString SnapshotWithSaleCount(int32 Count,bool DuplicateLast=false)
{
    TSharedPtr<FJsonObject> O;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Snapshot()),O)||!O)return FString();
    const auto Base=O->GetArrayField(TEXT("saleItems"))[0]->AsObject();TArray<TSharedPtr<FJsonValue>> Rows;
    for(int32 Index=0;Index<Count;++Index)
    {
        auto Row=MakeShared<FJsonObject>(*Base);
        Row->SetStringField(TEXT("itemId"),Id(FGuid(1,2,3,DuplicateLast&&Index==Count-1?1:Index+1)));
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    O->SetArrayField(TEXT("saleItems"),Rows);FString Result;
    FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Result));return Result;
}
FString Receipt(const FRPGShopIntent& I)
{
    const int64 Total=I.UnitPrice*I.Quantity,After=I.Action==TEXT("buy")?1000-Total:1000+Total;
    return FString::Printf(TEXT(R"({"requestId":"%s","characterId":"%s","shopId":"%s","policyTag":"%s","action":"%s","offerId":"%s","itemId":%s,"quantity":%d,"unitPrice":"%lld","totalPrice":"%lld","currencyCode":"%s","balanceBefore":"1000","balanceAfter":"%lld","committedAtUtc":"2026-11-17T00:00:00Z"})"),
        *Id(I.RequestId),*Id(I.Scope.Character),*I.ShopId,*I.PolicyTag,*I.Action,*I.OfferId,I.ItemId.IsValid()?*FString::Printf(TEXT("\"%s\""),*Id(I.ItemId)):TEXT("null"),I.Quantity,I.UnitPrice,Total,*I.CurrencyCode,After);
}
FString Assets(const FRPGShopIntent& I)
{
    return FString::Printf(TEXT(R"({"requestId":"%s","characterId":"%s","observedAt":"2026-11-17T00:00:01Z","isComplete":true,"wallet":{"characterId":"%s","rosterId":"00000004-0000-0004-0000-000400000004","accountId":"%s","balances":[{"currencyCode":"Gold","displayName":"Gold","scope":"Account","ownerId":"%s","balance":900,"maxBalance":9223372036854775807,"revision":9007199254740995}]},"items":[]})"),*Id(I.RequestId),*Id(I.Scope.Character),*Id(I.Scope.Character),*I.Scope.Account,*I.Scope.Account);
}
FString Rejection(const FRPGShopIntent& I)
{
    return FString::Printf(TEXT(R"({"rejected":true,"error":"shop_price_changed","requestId":"%s","characterId":"%s","shopId":"%s","policyTag":"%s","action":"%s","offerId":"%s","quantity":%d,"expectedUnitPrice":"%lld","itemId":%s,"expectedItemRevision":%s,"rejectedAtUtc":"2026-11-17T00:00:00Z"})"),
        *Id(I.RequestId),*Id(I.Scope.Character),*I.ShopId,*I.PolicyTag,*I.Action,*I.OfferId,I.Quantity,I.UnitPrice,
        I.ItemId.IsValid()?*FString::Printf(TEXT("\"%s\""),*Id(I.ItemId)):TEXT("null"),I.ItemRevision>0?*FString::Printf(TEXT("\"%lld\""),I.ItemRevision):TEXT("null"));
}
struct FFake final:IRPGShopTransport
{
    FRPGShopContext& Context;FCompletion Completion;FRPGShopIntent Last;FString Root;int32 Posts=0,ShopReads=0,ReceiptReads=0,AssetReads=0;bool bSaved=false;
    FFake(FRPGShopContext& C,const FString& R):Context(C),Root(R){}
    virtual bool GetContext(FString& E,FString& A)const override{E=Context.Endpoint;A=Context.Account;return !A.IsEmpty();}
    virtual bool CanSend()const override{return Context.bAuthenticated&&!Completion;}
    virtual bool ReadShop(FGuid,FCompletion D)override{++ShopReads;Completion=MoveTemp(D);return true;}
    virtual bool SendIntent(const FRPGShopIntent& I,FCompletion D)override
    {Last=I;++Posts;FRPGShopIntent Saved;bSaved=RPGShopJournal::Load(Root,I.Scope,Saved)==ERPGShopJournal::Ready&&Saved.Same(I);Completion=MoveTemp(D);return true;}
    virtual bool ReadReceipt(const FRPGShopIntent& I,FCompletion D)override{Last=I;++ReceiptReads;Completion=MoveTemp(D);return true;}
    virtual bool ReadAssets(const FRPGShopIntent& I,FCompletion D)override{Last=I;++AssetReads;Completion=MoveTemp(D);return true;}
    virtual void Cancel()override{Completion={};}
    void Reply(int32 Code,const FString& Body=FString(),double Delay=0){auto D=MoveTemp(Completion);if(D)D({true,Code!=0,Code,Body,Delay});}
};
struct FSetup
{
    FRPGShopContext Context{TEXT("https://shop.example/api"),Account,Character,true,true,true};
    FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/Shop")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    double Time=100;bool bAssetCurrent=true;int32 Applies=0;
    TSharedRef<FFake> Fake=MakeShared<FFake>(Context,Root);TSharedPtr<FRPGShopCoordinator> Owner;
    FSetup(){Create();}
    void Create()
    {
        Owner=MakeShared<FRPGShopCoordinator>(Fake,[this]{return Context;},Root,[this]{return Time;});
        Owner->SetAssetSink([]{return true;},[this](const FRPGTradeAssets&){++Applies;return bAssetCurrent;},[this]{return bAssetCurrent;});
    }
    void Read(){Owner->Refresh();Fake->Reply(200,Snapshot());}
    ~FSetup()
    {
        if(Owner)Owner->Shutdown();Owner.Reset();auto& P=FPlatformFileManager::Get().GetPlatformFile();
        // Only known direct files in this test's fresh GUID directory; never recurse through a link.
        if(P.IsSymlink(*Root)==ESymlinkResult::NonSymlink)
        {TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(Root/TEXT("*")),true,false);for(const auto& File:Files){const FString Path=Root/File;if(P.IsSymlink(*Path)==ESymlinkResult::NonSymlink)P.DeleteFile(*Path);}P.DeleteDirectory(*Root);}
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopCodecTest,"ProjectRPG.Shop.Codec",RPGShopTests::Flags)
bool FRPGShopCodecTest::RunTest(const FString&)
{
    using namespace RPGShopTests;FRPGShopSnapshot S;TestTrue(TEXT("snapshot accepted"),RPGShopCodec::Snapshot(Snapshot(),S));
    if(!TestEqual(TEXT("sale row"),S.SaleItems.Num(),1))return false;TestEqual(TEXT("revision exact"),S.SaleItems[0].Revision,9007199254740993LL);
    TestEqual(TEXT("server maximum"),S.Offers[0].MaximumPurchasable,10);
    for(int32 Count:{251,500})
    {
        TestTrue(FString::Printf(TEXT("owned rows beyond bag capacity accepted: %d"),Count),RPGShopCodec::Snapshot(SnapshotWithSaleCount(Count),S));
        TestEqual(TEXT("all owned rows preserved"),S.SaleItems.Num(),Count);
    }
    TestFalse(TEXT("owned record cap 501 rejected"),RPGShopCodec::Snapshot(SnapshotWithSaleCount(501),S));
    TestFalse(TEXT("duplicate item rejected at owned cap"),RPGShopCodec::Snapshot(SnapshotWithSaleCount(500,true),S));
    for(const auto& Bad:TArray<FString>{Snapshot().Replace(TEXT("\"balance\":\"1000\""),TEXT("\"balance\":1000")),Snapshot().Replace(TEXT("\"maximumPurchasable\":10"),TEXT("\"maximumPurchasable\":11")),Snapshot().Replace(TEXT("\"maximumQuantity\":10"),TEXT("\"maximumQuantity\":1.5")),Snapshot().Replace(TEXT("\"balance\":\"1000\""),TEXT("\"balance\":\"1000\",\"balance\":\"1\"")),Snapshot().Replace(TEXT("[80,79,79]"),TEXT("[80,81,79]"))})
        TestFalse(TEXT("invalid wire rejected"),RPGShopCodec::Snapshot(Bad,S));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopConsentTest,"ProjectRPG.Shop.Consent",RPGShopTests::Flags)
bool FRPGShopConsentTest::RunTest(const FString&)
{
    using namespace RPGShopTests;FSetup S;S.Read();auto V=S.Owner->GetView();TestTrue(TEXT("ready"),V.bCanTrade);
    TestFalse(TEXT("over server maximum"),S.Owner->Buy(V.SnapshotToken,TEXT("potion"),11));
    TestTrue(TEXT("review sale"),S.Owner->ReviewSale(V.SnapshotToken,Item,2));auto Review=S.Owner->GetView();TestTrue(TEXT("review only, no POST"),Review.bCanConfirm&&S.Fake->Posts==0);
    S.Owner->DismissSale();TestFalse(TEXT("dismiss invalidates consent"),S.Owner->ConfirmSale(Review.ReviewToken));
    S.Owner->ReviewSale(V.SnapshotToken,Item,2);Review=S.Owner->GetView();S.Time+=61;
    TestFalse(TEXT("expired snapshot cannot authorize"),S.Owner->ConfirmSale(Review.ReviewToken));S.Read();V=S.Owner->GetView();
    TestTrue(TEXT("buy original"),S.Owner->Buy(V.SnapshotToken,TEXT("potion"),2));TestTrue(TEXT("durable before dispatch"),S.Fake->bSaved);
    TestTrue(TEXT("browsing preserved during purchase"),S.Owner->GetView().bHasSnapshot);TestFalse(TEXT("pending name is not technical offer id"),S.Owner->GetView().PendingText.ToString().Contains(TEXT("potion")));
    TestFalse(TEXT("repeated action blocked"),S.Owner->Buy(V.SnapshotToken,TEXT("potion"),2));TestEqual(TEXT("one POST"),S.Fake->Posts,1);
    S.Fake->Reply(409,Rejection(S.Fake->Last));TestFalse(TEXT("durable refusal releases"),S.Owner->BlocksTransitions());
    TestFalse(TEXT("requires fresh catalog after rejection"),S.Owner->Buy(V.SnapshotToken,TEXT("potion"),2));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopRecoveryTest,"ProjectRPG.Shop.Recovery",RPGShopTests::Flags)
bool FRPGShopRecoveryTest::RunTest(const FString&)
{
    using namespace RPGShopTests;FSetup S;S.Read();auto V=S.Owner->GetView();S.Owner->Buy(V.SnapshotToken,TEXT("potion"),2);const auto Original=S.Fake->Last;
    S.Fake->Reply(503,TEXT("{}"),3);TestTrue(TEXT("unknown blocks transitions"),S.Owner->BlocksTransitions());TestFalse(TEXT("retry-after respected"),S.Owner->Retry());S.Time+=4;
    TestTrue(TEXT("retry same command"),S.Owner->Retry());TestTrue(TEXT("exact intent"),S.Fake->Last.Same(Original));S.Fake->Reply(409,TEXT("{\"error\":\"shop_insufficient_balance\"}"));
    TestTrue(TEXT("later rejection cannot erase unknown commit"),S.Owner->BlocksTransitions());S.Owner->Shutdown();S.Owner.Reset();S.Create();S.Owner->Tick();
    TestEqual(TEXT("restart never auto submits"),S.Fake->Posts,2);TestTrue(TEXT("restart restores pending"),S.Owner->GetView().bHasPending);
    TestTrue(TEXT("explicit receipt read"),S.Owner->Recover());TestTrue(TEXT("same original after restart"),S.Fake->Last.Same(Original));
    S.Fake->Reply(200,Receipt(Original));TestEqual(TEXT("fresh assets requested"),S.Fake->AssetReads,1);TestTrue(TEXT("receipt alone retains barrier"),S.Owner->BlocksTransitions());
    S.Fake->Reply(200,Assets(Original).Replace(*Account,TEXT("76561198000000001")));TestEqual(TEXT("wrong owner not applied"),S.Applies,0);TestTrue(TEXT("retry fresh assets"),S.Owner->RefreshAssets());
    S.Fake->Reply(200,Assets(Original));TestEqual(TEXT("fresh assets applied once"),S.Applies,1);TestFalse(TEXT("fresh state releases barrier"),S.Owner->BlocksTransitions());
    TestEqual(TEXT("recovery never resubmits new purchase"),S.Fake->Posts,2);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopScopeTest,"ProjectRPG.Shop.ScopeAndReceipt",RPGShopTests::Flags)
bool FRPGShopScopeTest::RunTest(const FString&)
{
    using namespace RPGShopTests;FSetup S;S.Read();const auto V=S.Owner->GetView();S.Owner->Buy(V.SnapshotToken,TEXT("potion"),1);const auto I=S.Fake->Last;auto Late=MoveTemp(S.Fake->Completion);
    S.Context.Character=FGuid(100,101,102,103);S.Owner->Tick();TestFalse(TEXT("different selected character unavailable"),S.Owner->GetView().bAvailable);
    Late({true,true,200,Receipt(I),0});TestEqual(TEXT("late result never imports"),S.Fake->AssetReads,0);TestTrue(TEXT("original remains durable"),S.Owner->BlocksTransitions());
    S.Context.Character=Character;S.Owner->Tick();TestTrue(TEXT("original scope recovers"),S.Owner->Recover());S.Fake->Reply(200,Receipt(I).Replace(TEXT("\"quantity\":1"),TEXT("\"quantity\":2")));
    TestTrue(TEXT("mismatched receipt unresolved"),S.Owner->BlocksTransitions());TestEqual(TEXT("mismatch never imports"),S.Fake->AssetReads,0);
    FRPGShopReceipt R;TestFalse(TEXT("receipt balance corruption rejected"),RPGShopCodec::Receipt(Receipt(I).Replace(TEXT("\"balanceAfter\":\"950\""),TEXT("\"balanceAfter\":\"999\"")),I,R));
    TestTrue(TEXT("receipt exact accepted"),RPGShopCodec::Receipt(Receipt(I),I,R));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGShopRejectedRecoveryTest,"ProjectRPG.Shop.RejectedRecovery",RPGShopTests::Flags)
bool FRPGShopRejectedRecoveryTest::RunTest(const FString&)
{
    using namespace RPGShopTests;FSetup S;S.Read();const auto V=S.Owner->GetView();S.Owner->Buy(V.SnapshotToken,TEXT("potion"),1);const auto I=S.Fake->Last;S.Fake->Reply(0);
    TestTrue(TEXT("lost response retained"),S.Owner->BlocksTransitions());S.Owner->Recover();
    S.Fake->Reply(409,Rejection(I).Replace(TEXT("\"quantity\":1"),TEXT("\"quantity\":2")));TestTrue(TEXT("other rejection cannot resolve"),S.Owner->BlocksTransitions());
    S.Owner->Recover();S.Fake->Reply(409,Rejection(I));TestFalse(TEXT("durable exact rejection resolves uncertain"),S.Owner->BlocksTransitions());
    TestFalse(TEXT("old snapshot cannot resubmit"),S.Owner->Buy(V.SnapshotToken,TEXT("potion"),1));TestEqual(TEXT("no compensating/new mutation"),S.Fake->Posts,1);
    S.Owner->Shutdown();S.Owner.Reset();S.Create();S.Owner->Tick();TestFalse(TEXT("cleared terminal rejection survives restart"),S.Owner->BlocksTransitions());return true;
}
#endif
