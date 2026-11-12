#if WITH_DEV_AUTOMATION_TESTS
#include "Auction/RPGAuctionCoordinator.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace AuctionRecoveryTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FString Endpoint = TEXT("https://auction.example/api"), Account = TEXT("76561198880000002");
const FGuid Character(11,12,13,14);
FString Root(const TCHAR* Name)
{ FString R; FParse::Value(FCommandLine::Get(), TEXT("AuctionJournalTestRoot="), R); return R.IsEmpty() || FPaths::IsRelative(R) ? FString() : R / Name; }
FString Fixture(const TCHAR* Name)
{ FString S; FFileHelper::LoadFileToString(S, *(FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_AUCTION_WIRE_FIXTURE")) / (FString(Name)+TEXT(".json")))); return S; }
FRPGAuctionListing Listing() { FRPGAuctionListing L; RPGAuction::DecodeListing(Fixture(TEXT("listing")), false, L); return L; }
FRPGAuctionLobbyContext Context()
{ return {Endpoint, Account, Character, true, true, true, true}; }
FRPGAuctionIntent Intent()
{ FRPGAuctionClient C; C.Bind(Endpoint,Account,Character); C.PreparePurchase(Listing()); FRPGAuctionIntent I; C.ExportPending(I); return I; }
FString Path(const FString& R, const FRPGTradeIntentScope& S)
{ FString Key; FRPGAuctionIntentJournal::ScopeKey(S,Key); return R/(Key+TEXT(".json")); }
FString Receipt(const FRPGAuctionIntent& I)
{
    TSharedPtr<FJsonObject> O;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Fixture(TEXT("purchase"))),O)) return TEXT("");
    O->SetStringField(TEXT("requestId"), I.Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
    FString Out; FJsonSerializer::Serialize(O.ToSharedRef(),TJsonWriterFactory<>::Create(&Out)); return Out;
}
struct FFake final : IRPGAuctionTransport
{
    FRPGAuctionLobbyContext* Current;
    FCompletion Completion;
    FRPGAuctionIntent Last;
    int32 Posts=0, Reads=0, Cancels=0;
    bool bAccept=true, bSavedBeforeSend=false;
    FString JournalRoot;
    explicit FFake(FRPGAuctionLobbyContext& C, const FString& R) : Current(&C), JournalRoot(R) {}
    virtual bool GetContext(FString& E,FString& A) const override { E=Current->Endpoint;A=Current->Account;return !A.IsEmpty(); }
    virtual bool CanSend() const override { return Current->bAuthenticated && !Completion; }
    virtual bool SendIntent(const FRPGAuctionIntent& I,FCompletion Done) override
    { ++Posts; Last=I;FRPGAuctionIntent Stored;bSavedBeforeSend=FRPGAuctionIntentJournal::Load(JournalRoot,I.Scope,Stored)==ERPGAuctionJournalRead::Ready && Stored.Request.Body==I.Request.Body;
      if(bAccept)Completion=MoveTemp(Done);return bAccept; }
    virtual bool SendReceipt(FGuid,FGuid,FCompletion Done) override { ++Reads;if(bAccept)Completion=MoveTemp(Done);return bAccept; }
    virtual bool SendPage(FGuid,const FRPGAuctionSearch&,bool,FCompletion) override {return false;}
    virtual bool SendListing(FGuid,FGuid,FCompletion) override {return false;}
    virtual bool SendPayable(FGuid,FGuid,FCompletion) override {return false;}
    virtual void Cancel() override { ++Cancels;Completion={}; }
    void Reply(int32 Code,const FString& Body=TEXT(""),double Delay=0)
    { auto Done=MoveTemp(Completion);if(Done)Done({true,Code!=0,Code,Body,Delay}); }
};
TSharedRef<FRPGAuctionCoordinator> Owner(TSharedRef<FFake> T,FRPGAuctionLobbyContext& C,const FString& R,double& Time)
{return MakeShared<FRPGAuctionCoordinator>(T,[&C]{return C;},R,[&Time]{return Time;});}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionJournalStorage,"ProjectRPG.Auction.Recovery.Storage",AuctionRecoveryTests::Flags)
bool FAuctionJournalStorage::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;const auto R=Root(TEXT("Storage"));if(!TestFalse(TEXT("owned root"),R.IsEmpty()))return false;
    const auto I=Intent();FRPGAuctionIntent Loaded;
    TestTrue(TEXT("valid original"),I.IsValid());
    TestTrue(TEXT("missing"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Missing);
    TestTrue(TEXT("create"),FRPGAuctionIntentJournal::Create(R,I));
    FString Raw;FFileHelper::LoadFileToString(Raw,*Path(R,I.Scope));
    TestFalse(TEXT("endpoint/account omitted"),Raw.Contains(Endpoint)||Raw.Contains(Account));
    TestFalse(TEXT("never overwrite existing"),FRPGAuctionIntentJournal::Create(R,I));
    TestTrue(TEXT("load"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Ready);
    TestEqual(TEXT("exact body"),Loaded.Request.Body,I.Request.Body);
    TestTrue(TEXT("restored delivery unknown"),Loaded.Request.Delivery==ERPGTradeDelivery::Unknown);
    auto Wrong=I;Wrong.Request.Body+=TEXT(" ");
    TestFalse(TEXT("compare clear exact bytes"),FRPGAuctionIntentJournal::Clear(R,Wrong));
    auto Other=I.Scope;Other.Account=TEXT("other");
    TestTrue(TEXT("scope isolation"),FRPGAuctionIntentJournal::Load(R,Other,Loaded)==ERPGAuctionJournalRead::Missing);
    FFileHelper::SaveStringToFile(Raw,*Path(R,Other),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("copy cannot cross scope"),FRPGAuctionIntentJournal::Load(R,Other,Loaded)==ERPGAuctionJournalRead::Invalid);
    TestTrue(TEXT("matching clear"),FRPGAuctionIntentJournal::Clear(R,I));
    for(const auto& Bad : {Raw.Replace(TEXT("\"version\":\"1\""),TEXT("\"version\":\"2\"")),Raw.Replace(TEXT("\"purchase\""),TEXT("\"cancel\"")),TEXT("{\"version\":\"1\",")+Raw.Mid(1),FString::ChrN(65537,TEXT('x'))})
    {
        FFileHelper::SaveStringToFile(Bad,*Path(R,I.Scope),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TestTrue(TEXT("tamper/duplicate/version/oversize invalid"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Invalid);
        TestFalse(TEXT("invalid file preserved"),FRPGAuctionIntentJournal::Create(R,I)||FRPGAuctionIntentJournal::Clear(R,I));
        FString After;FFileHelper::LoadFileToString(After,*Path(R,I.Scope));TestEqual(TEXT("bytes untouched"),After,Bad);
    }
    TestTrue(TEXT("relative unavailable"),FRPGAuctionIntentJournal::Load(TEXT("relative"),I.Scope,Loaded)==ERPGAuctionJournalRead::Unavailable);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionJournalLock,"ProjectRPG.Auction.Recovery.Locking",AuctionRecoveryTests::Flags)
bool FAuctionJournalLock::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;const auto R=Root(TEXT("Locking"));if(R.IsEmpty())return false;const auto I=Intent();FRPGAuctionIntent Loaded;
    TestTrue(TEXT("create"),FRPGAuctionIntentJournal::Create(R,I));FString Key;FRPGAuctionIntentJournal::ScopeKey(I.Scope,Key);
    TUniquePtr<IFileHandle> Lock(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(R/(Key+TEXT(".lock"))),false,false));
    if(!TestTrue(TEXT("exclusive lock"),!!Lock))return false;
    TestTrue(TEXT("reader busy"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Busy);
    TestFalse(TEXT("writer blocked"),FRPGAuctionIntentJournal::Create(R,I));TestFalse(TEXT("clear blocked"),FRPGAuctionIntentJournal::Clear(R,I));Lock.Reset();
    TestTrue(TEXT("lock release intact"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Ready);
    TestTrue(TEXT("junction rejected"),FRPGAuctionIntentJournal::Load(Root(TEXT("Linked")),I.Scope,Loaded)==ERPGAuctionJournalRead::Unavailable);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCoordinatorRecovery,"ProjectRPG.Auction.Recovery.Coordinator",AuctionRecoveryTests::Flags)
bool FAuctionCoordinatorRecovery::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;auto C=Context();double Time=100;const auto R=Root(TEXT("Coordinator"));if(R.IsEmpty())return false;
    auto T=MakeShared<FFake>(C,R);auto O=Owner(T,C,R,Time);O->Tick();
    TestEqual(TEXT("load never sends"),T->Posts+T->Reads,0);
    TestTrue(TEXT("purchase durable"),O->Purchase(Listing()));TestTrue(TEXT("write precedes HTTP"),T->bSavedBeforeSend);
    const auto Original=T->Last;T->Reply(503,TEXT("{}"),30);
    Time=129;TestFalse(TEXT("retry delay"),O->Retry());TestFalse(TEXT("receipt cannot bypass delay"),O->Recover());
    Time=1;TestFalse(TEXT("clock rollback"),O->Retry());Time=130;
    int32 Notifications=0;O->OnChanged=[&Notifications]{++Notifications;};
    TestTrue(TEXT("explicit receipt"),O->Recover());TestTrue(TEXT("busy state published before completion"),Notifications>0 && O->IsBusy());
    T->Reply(404,TEXT("{\"error\":\"auction_not_found\"}"));
    TestNotNull(TEXT("404 is not failure proof"),O->GetClient().GetPending());Time=131;
    TestTrue(TEXT("explicit original replay"),O->Retry());TestEqual(TEXT("body retained"),T->Last.Request.Body,Original.Request.Body);T->Reply(401);
    C.bAuthenticated=false;Time=200;O->Tick();TestFalse(TEXT("auth waits"),O->Retry());C.bAuthenticated=true;
    TestTrue(TEXT("token renewal retry"),O->Retry());T->Reply(200,Receipt(Original));
    TestTrue(TEXT("matching success"),O->GetLastResponse()==ERPGAuctionResponse::Succeeded);
    FRPGAuctionIntent Saved;TestTrue(TEXT("cleared only after matching receipt"),FRPGAuctionIntentJournal::Load(R,Original.Scope,Saved)==ERPGAuctionJournalRead::Missing);
    TestTrue(TEXT("historical assets require refresh"),O->GetClient().NeedsAssetRefresh());TestFalse(TEXT("new purchase waits for refresh"),O->Purchase(Listing()));
    TestFalse(TEXT("wrong receipt cannot acknowledge assets"),O->AcknowledgeAssets(FGuid::NewGuid()));
    TestTrue(TEXT("projection owner acknowledges matching receipt"),O->AcknowledgeAssets(Original.Request.RequestId));
    TestFalse(TEXT("resolved transitions released"),O->BlocksTransitions());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCoordinatorContext,"ProjectRPG.Auction.Recovery.Context",AuctionRecoveryTests::Flags)
bool FAuctionCoordinatorContext::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;auto C=Context();double Time=100;const auto R=Root(TEXT("Context"));if(R.IsEmpty())return false;
    auto T=MakeShared<FFake>(C,R);auto O=Owner(T,C,R,Time);O->Tick();O->Purchase(Listing());const auto I=T->Last;auto Late=T->Completion;
    C.Character=FGuid(9,9,9,9);O->Tick();Late({true,true,200,Receipt(I),0});
    TestNull(TEXT("old character callback ignored"),O->GetClient().GetReceipt());TestFalse(TEXT("new character unblocked"),O->BlocksTransitions());
    C.Character=Character;O->Tick();TestNotNull(TEXT("reselect restores original"),O->GetClient().GetPending());
    TestTrue(TEXT("restored intent blocks transition"),O->BlocksTransitions());TestEqual(TEXT("no auto dispatch"),T->Posts,1);
    C.bEnabled=false;Time=200;TestTrue(TEXT("disabled still permits recovery"),O->Recover());T->Reply(200,Receipt(I));O->AcknowledgeAssets(I.Request.RequestId);
    TestFalse(TEXT("disabled prevents new sale"),O->Purchase(Listing()));
    C.bEnabled=true;O->Purchase(Listing());Late=T->Completion;
    C.bLocalLobby=false;O->Tick();Late({true,true,200,Receipt(T->Last),0});TestNull(TEXT("travel response ignored"),O->GetClient().GetReceipt());
    C.bLocalLobby=true;O->Tick();TestTrue(TEXT("return restores journal"),O->BlocksTransitions());
    C.Account=TEXT("76561198880000003");O->Tick();TestNull(TEXT("account change discards in-memory intent"),O->GetClient().GetPending());
    TestFalse(TEXT("stale selected character cannot authorize new account"),O->Purchase(Listing()));
    C.Character.Invalidate();O->Tick();C.Character=Character;O->Tick();TestNull(TEXT("other account cannot load old journal"),O->GetClient().GetPending());
    O->OnChanged=[O]{O->Shutdown();};TestTrue(TEXT("reentrant shutdown after durable prepare"),O->Purchase(Listing()));TestFalse(TEXT("shutdown cannot retry"),O->Retry());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionCoordinatorStorage,"ProjectRPG.Auction.Recovery.StorageFailure",AuctionRecoveryTests::Flags)
bool FAuctionCoordinatorStorage::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;auto C=Context();double Time=100;const auto R=Root(TEXT("StorageFailure"));if(R.IsEmpty())return false;
    auto I=Intent();FRPGAuctionIntentJournal::Create(R,I);const auto File=Path(R,I.Scope);
    FFileHelper::SaveStringToFile(TEXT("broken"),*File,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    auto T=MakeShared<FFake>(C,R);auto O=Owner(T,C,R,Time);O->Tick();
    TestTrue(TEXT("invalid journal blocks transitions"),O->BlocksTransitions());TestTrue(TEXT("storage blocked exposed"),O->IsStorageBlocked());
    TestFalse(TEXT("invalid journal prevents replacement"),O->Purchase(Listing()));TestFalse(TEXT("invalid journal prevents dispatch"),O->Retry());TestEqual(TEXT("zero network"),T->Posts+T->Reads,0);
    FString Raw;FFileHelper::LoadFileToString(Raw,*File);TestEqual(TEXT("invalid bytes preserved"),Raw,FString(TEXT("broken")));
    // A known result is kept in memory until compare-clear succeeds; a changed file cannot be cleared.
    const auto ClearRoot=Root(TEXT("ClearFailure"));auto U=MakeShared<FFake>(C,ClearRoot);auto P=Owner(U,C,ClearRoot,Time);P->Purchase(Listing());const auto Original=U->Last;
    FString ValidFile;FFileHelper::LoadFileToString(ValidFile,*Path(ClearRoot,Original.Scope));
    FFileHelper::SaveStringToFile(TEXT("broken"),*Path(ClearRoot,Original.Scope),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    U->Reply(200,Receipt(Original));TestTrue(TEXT("clear failure retained"),P->IsStorageBlocked()&&P->BlocksTransitions());TestFalse(TEXT("cannot replace after clear failure"),P->Purchase(Listing()));
    FFileHelper::SaveStringToFile(ValidFile,*Path(ClearRoot,Original.Scope),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    int32 Changes=0;P->OnChanged=[&Changes]{++Changes;};
    TestFalse(TEXT("repair resolves storage without replaying known success"),P->Recover());
    TestTrue(TEXT("storage resolution published even without network"),Changes>0 && !P->IsStorageBlocked() && !P->BlocksTransitions());
    auto V=MakeShared<FFake>(C,Root(TEXT("Rejected")));auto Q=Owner(V,C,V->JournalRoot,Time);Q->Purchase(Listing());const auto Rejected=V->Last;
    V->Reply(409,TEXT("{\"error\":\"auction_revision_conflict\"}"));
    TestTrue(TEXT("known first refusal"),Q->GetLastResponse()==ERPGAuctionResponse::Rejected);
    FRPGAuctionIntent Loaded;TestTrue(TEXT("known refusal clears its original"),FRPGAuctionIntentJournal::Load(V->JournalRoot,Rejected.Scope,Loaded)==ERPGAuctionJournalRead::Missing);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuctionJournalRestart,"ProjectRPG.Auction.Recovery.Restart",AuctionRecoveryTests::Flags)
bool FAuctionJournalRestart::RunTest(const FString&)
{
    using namespace AuctionRecoveryTests;const auto R=Root(TEXT("Restart"));if(R.IsEmpty())return false;
    FString Phase;FParse::Value(FCommandLine::Get(),TEXT("AuctionJournalPhase="),Phase);auto C=Context();double Time=100;
    auto T=MakeShared<FFake>(C,R);auto O=Owner(T,C,R,Time);
    if(Phase==TEXT("write"))
    {
        TestTrue(TEXT("writer persists before uncertain HTTP"),O->Purchase(Listing()));TestTrue(TEXT("disk before send"),T->bSavedBeforeSend);
        TestTrue(TEXT("save independent expected bytes"),FFileHelper::SaveStringToFile(T->Last.Request.Body,*(R/TEXT("Expected.body")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
        T->Reply(0);O->Shutdown();return true;
    }
    if(!TestEqual(TEXT("separate reader phase"),Phase,FString(TEXT("read"))))return false;
    O->Tick();TestEqual(TEXT("second process never automatically sends"),T->Posts+T->Reads,0);
    const auto* Pending=O->GetClient().GetPending();if(!TestNotNull(TEXT("restart restores uncertain original"),Pending))return false;
    FString Expected;FFileHelper::LoadFileToString(Expected,*(R/TEXT("Expected.body")));TestEqual(TEXT("original bytes survived process exit"),Pending->Body,Expected);
    TestTrue(TEXT("reader dispatches explicit receipt"),O->Recover());T->Reply(404,TEXT("{\"error\":\"auction_not_found\"}"));Time=101;
    TestTrue(TEXT("reader explicitly replays original"),O->Retry());TestEqual(TEXT("replay same bytes"),T->Last.Request.Body,Expected);const auto I=T->Last;
    T->Reply(200,Receipt(I));TestTrue(TEXT("matching receipt resolves"),O->GetLastResponse()==ERPGAuctionResponse::Succeeded);
    FRPGAuctionIntent Loaded;TestTrue(TEXT("reader compare-clears"),FRPGAuctionIntentJournal::Load(R,I.Scope,Loaded)==ERPGAuctionJournalRead::Missing);return true;
}
#endif
