#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeIntentJournal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace RPGTradeJournalTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid A(1,1,1,1),B(2,2,2,2),Trade(3,3,3,3),Item(4,4,4,4);
const FString Endpoint=TEXT("https://trade.example/api"),Account=TEXT("76561198000000001");
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Session(bool Invited=false,FGuid T=Trade)
{
    return FString::Printf(TEXT(R"({"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"9","offerRevision":"3","expiresAt":"2030-01-01T00:00:00Z","status":"%s","completed":false,"initiatorOffer":{"currencyAmount":"%s","items":[]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":%s,"inviteeReady":%s,"initiatorConfirmed":false,"inviteeConfirmed":false})"),*Id(T),*Id(A),*Id(B),Invited?TEXT("Invited"):TEXT("Negotiating"),Invited?TEXT("0"):TEXT("9007199254740993"),Invited?TEXT("false"):TEXT("true"),Invited?TEXT("false"):TEXT("true"));
}
FRPGTradeClient Client(FGuid Actor=A,bool WithSession=false,bool Invited=false)
{
    FRPGTradeClient C;C.Bind(Endpoint,Account,Actor);
    if(WithSession)C.CompleteRead(C.BeginRead(),200,TEXT("{\"session\":")+Session(Invited)+TEXT("}"));return C;
}
FRPGTradePendingIntent Intent()
{
    auto C=Client(A,true);FRPGTradeOffer Offer;Offer.CurrencyAmount=9007199254740993LL;Offer.Items={{Item,9007199254740995LL,3}};
    C.Prepare(ERPGTradeAction::Offer,&Offer);FRPGTradePendingIntent I;C.ExportPending(I);return I;
}
FString Root(const TCHAR* Case)
{
    FString Base;FParse::Value(FCommandLine::Get(),TEXT("TradeJournalTestRoot="),Base);
    return FPaths::IsRelative(Base)||Base.IsEmpty()?FString():Base/Case;
}
FString Path(const FString& Directory,const FRPGTradeIntentScope& Scope)
{FString Key;FRPGTradeIntentJournal::ScopeKey(Scope,Key);return Directory/(Key+TEXT(".json"));}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeIntentValidationTest,"ProjectRPG.Trading.Journal.Validation",RPGTradeJournalTests::Flags)
bool FTradeIntentValidationTest::RunTest(const FString&)
{
    using namespace RPGTradeJournalTests;const auto Good=Intent();TestTrue(TEXT("generated offer valid"),Good.IsValid());
    for(const auto Action:{ERPGTradeAction::Invite,ERPGTradeAction::Accept,ERPGTradeAction::Offer,ERPGTradeAction::Ready,ERPGTradeAction::Withdraw,ERPGTradeAction::Confirm,ERPGTradeAction::Cancel})
    {
        auto C=Client(Action==ERPGTradeAction::Accept?B:A,Action!=ERPGTradeAction::Invite,Action==ERPGTradeAction::Accept);
        const FRPGTradeOffer Offer{9007199254740993LL,{{Item,9007199254740995LL,3}}};
        const bool Prepared=Action==ERPGTradeAction::Invite?C.PrepareInvite(B):C.Prepare(Action,Action==ERPGTradeAction::Offer?&Offer:nullptr);
        if(!TestTrue(TEXT("all seven original command shapes prepare"),Prepared))return false;
        FRPGTradePendingIntent I;TestTrue(TEXT("all command shapes export"),C.ExportPending(I));
        auto Restored=Client(I.Scope.Character);TestTrue(TEXT("all command shapes restore"),Restored.RestorePending(I));
        TestTrue(TEXT("always unknown on restore"),Restored.GetPending() && Restored.GetPending()->Delivery==ERPGTradeDelivery::Unknown);
        if(Restored.GetPending())TestEqual(TEXT("original request bytes preserved"),Restored.GetPending()->Body,I.Request.Body);
    }
    auto Spaced=Good;Spaced.Request.Body=TEXT(" \n")+Spaced.Request.Body+TEXT("\n ");
    auto C=Client();TestTrue(TEXT("valid original whitespace retained"),C.RestorePending(Spaced));
    TestEqual(TEXT("no reserialization"),C.GetPending()->Body,Spaced.Request.Body);
    TArray<FRPGTradePendingIntent> Bad;
    auto Add=[&](const FString& Body){auto I=Good;I.Request.Body=Body;Bad.Add(I);};
    Add(Good.Request.Body.Replace(TEXT("\"quantity\":3"),TEXT("\"quantity\":3.0")));
    Add(Good.Request.Body.Replace(TEXT("\"quantity\":3"),TEXT("\"quantity\":3e0")));
    Add(Good.Request.Body.Replace(TEXT("\"quantity\":3"),TEXT("\"quantity\":\"3\"")));
    Add(Good.Request.Body.Replace(TEXT("\"expectedRevision\":\"9\""),TEXT("\"expectedRevision\":\"9223372036854775807\"")));
    Add(Good.Request.Body.Replace(TEXT("\"expectedRevision\":\"9\""),TEXT("\"expectedRevision\":9")));
    Add(TEXT("{\"Authorization\":\"credential\",")+Good.Request.Body.Mid(1));
    Add(TEXT("{\"requestId\":\"")+Id(Good.Request.RequestId)+TEXT("\",")+Good.Request.Body.Mid(1));
    Add(Good.Request.Body+FString::ChrN(17000,TEXT(' ')));
    auto Changed=Good;Changed.Request.RequestId=FGuid::NewGuid();Bad.Add(Changed);
    Changed=Good;Changed.Request.RelativePath+=TEXT("?token=secret");Bad.Add(Changed);
    Changed=Good;Changed.Request.RelativePath=TEXT("https://other.example/api");Bad.Add(Changed);
    Changed=Good;Changed.Scope.Character=B;Bad.Add(Changed);
    for(const auto& I:Bad){auto Fresh=Client();TestFalse(TEXT("invalid import rejected atomically"),Fresh.RestorePending(I));TestNull(TEXT("no partial pending"),Fresh.GetPending());}
    FRPGTradeIntentScope S;
    TestTrue(TEXT("canonical scope factory"),FRPGTradeIntentScope::Create(TEXT(" https://trade.example/api/ "),Account,A,S));
    TestEqual(TEXT("normalized endpoint"),S.Endpoint,Endpoint);
    for(const auto& Url:{TEXT("https://user:credential@trade.example/api"),TEXT("https://trade.example/api?token=x"),TEXT("https://trade.example/api#x"),TEXT("https://trade.example/ api")})
        TestFalse(TEXT("credential-bearing or ambiguous endpoint rejected"),FRPGTradeIntentScope::Create(Url,Account,A,S));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeIntentRecoveryTest,"ProjectRPG.Trading.Journal.Recovery",RPGTradeJournalTests::Flags)
bool FTradeIntentRecoveryTest::RunTest(const FString&)
{
    using namespace RPGTradeJournalTests;const auto I=Intent();auto C=Client();const auto OldRead=C.BeginRead();
    TestTrue(TEXT("fresh bound client imports original"),C.RestorePending(I));
    TestTrue(TEXT("late pre-restore callback invalidated"),C.CompleteRead(OldRead,200,TEXT("{\"session\":null}"))==ERPGTradeResponse::Ignored);
    TestTrue(TEXT("active null never resolves command"),C.CompleteRead(C.BeginRead(),200,TEXT("{\"session\":null}"))==ERPGTradeResponse::NoActiveTrade);
    TestNotNull(TEXT("pending survives active null"),C.GetPending());FRPGTradeTicket T;C.BeginAttempt(T);
    TestTrue(TEXT("first observed plain refusal is still uncertain after restart"),C.CompleteAttempt(T,404,TEXT("{\"error\":\"not_found\"}"))==ERPGTradeResponse::Unknown);
    TestFalse(TEXT("cannot replace existing intent"),C.RestorePending(I));
    FRPGTradePendingIntent Again;C.ExportPending(Again);TestEqual(TEXT("same bytes after refusal"),Again.Request.Body,I.Request.Body);
    C.BeginAttempt(T);
    const FString Receipt=TEXT("{\"requestId\":\"")+Id(I.Request.RequestId)+TEXT("\",\"session\":")+Session()+TEXT(",\"committedAt\":\"2026-11-10T00:00:00Z\",\"rejection\":null}");
    TestTrue(TEXT("matching receipt resolves restored request"),C.CompleteAttempt(T,200,Receipt)==ERPGTradeResponse::Succeeded);
    TestNull(TEXT("only matching result clears memory intent"),C.GetPending());
    auto Live=Client(A,true);TestFalse(TEXT("existing session cannot be overwritten"),Live.RestorePending(I));
    auto Other=Client(B);TestFalse(TEXT("other character cannot restore"),Other.RestorePending(I));
    Other.Bind(Endpoint,TEXT("other-account"),A);TestFalse(TEXT("other account cannot restore"),Other.RestorePending(I));
    Other.Bind(TEXT("https://other.example/api"),Account,A);TestFalse(TEXT("other endpoint cannot restore"),Other.RestorePending(I));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeIntentStorageTest,"ProjectRPG.Trading.Journal.Storage",RPGTradeJournalTests::Flags)
bool FTradeIntentStorageTest::RunTest(const FString&)
{
    using namespace RPGTradeJournalTests;const auto Directory=Root(TEXT("Storage"));if(!TestFalse(TEXT("runner-owned absolute fixture root required"),Directory.IsEmpty()))return false;
    const auto I=Intent();FRPGTradePendingIntent Loaded;
    TestTrue(TEXT("missing reported"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Missing);
    TestTrue(TEXT("create complete intent"),FRPGTradeIntentJournal::Create(Directory,I));
    FString Raw;FFileHelper::LoadFileToString(Raw,*Path(Directory,I.Scope));
    TestFalse(TEXT("raw account not persisted"),Raw.Contains(Account));TestFalse(TEXT("endpoint not persisted"),Raw.Contains(Endpoint));
    TestFalse(TEXT("existing intent not replaced even by same command"),FRPGTradeIntentJournal::Create(Directory,I));
    TestTrue(TEXT("read original"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Ready);
    TestEqual(TEXT("read body exact"),Loaded.Request.Body,I.Request.Body);
    auto Wrong=I;Wrong.Request.Body=Wrong.Request.Body.Replace(TEXT("9007199254740993"),TEXT("9007199254740994"));
    TestFalse(TEXT("compare clear checks full body"),FRPGTradeIntentJournal::Clear(Directory,Wrong));
    Wrong=I;Wrong.Request.RequestId=FGuid::NewGuid();TestFalse(TEXT("different request cannot clear"),FRPGTradeIntentJournal::Clear(Directory,Wrong));
    auto Scope=I.Scope;Scope.Account=TEXT("other-account");
    TestTrue(TEXT("other account gets separate missing path"),FRPGTradeIntentJournal::Load(Directory,Scope,Loaded)==ERPGTradeJournalRead::Missing);
    IFileManager::Get().Copy(*Path(Directory,Scope),*Path(Directory,I.Scope));
    TestTrue(TEXT("copied record cannot cross scope"),FRPGTradeIntentJournal::Load(Directory,Scope,Loaded)==ERPGTradeJournalRead::Invalid);
    TestTrue(TEXT("matching original compare clear"),FRPGTradeIntentJournal::Clear(Directory,I));
    TestTrue(TEXT("cleared path missing"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Missing);
    for(const FString& Bad:{Raw.Replace(TEXT("\"version\":\"1\""),TEXT("\"version\":\"2\"")),Raw.Replace(TEXT("9007199254740993"),TEXT("9007199254740994")),TEXT("{\"version\":\"1\",")+Raw.Mid(1),FString::ChrN(65537,TEXT('x'))})
    {
        FFileHelper::SaveStringToFile(Bad,*Path(Directory,I.Scope),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TestTrue(TEXT("invalid/version/tampered/oversized input blocked"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Invalid);
        TestFalse(TEXT("invalid record never replaced"),FRPGTradeIntentJournal::Create(Directory,I));TestFalse(TEXT("invalid record never deleted"),FRPGTradeIntentJournal::Clear(Directory,I));
        FString After;FFileHelper::LoadFileToString(After,*Path(Directory,I.Scope));TestEqual(TEXT("invalid bytes retained"),After,Bad);
    }
    TestTrue(TEXT("relative roots unavailable"),FRPGTradeIntentJournal::Load(TEXT("relative"),I.Scope,Loaded)==ERPGTradeJournalRead::Unavailable);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeIntentLockTest,"ProjectRPG.Trading.Journal.Locking",RPGTradeJournalTests::Flags)
bool FTradeIntentLockTest::RunTest(const FString&)
{
    using namespace RPGTradeJournalTests;const auto Directory=Root(TEXT("Locking"));if(Directory.IsEmpty())return false;
    const auto I=Intent();TestTrue(TEXT("create for lock test"),FRPGTradeIntentJournal::Create(Directory,I));FString Key;FRPGTradeIntentJournal::ScopeKey(I.Scope,Key);
    TUniquePtr<IFileHandle> Lock(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(Directory/(Key+TEXT(".lock"))),false,false));
    if(!TestTrue(TEXT("own exclusive lock acquired"),!!Lock))return false;
    FRPGTradePendingIntent Loaded;TestTrue(TEXT("concurrent reader blocked"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Busy);
    TestFalse(TEXT("concurrent writer blocked"),FRPGTradeIntentJournal::Create(Directory,I));TestFalse(TEXT("concurrent clear blocked"),FRPGTradeIntentJournal::Clear(Directory,I));Lock.Reset();
    TestTrue(TEXT("record intact after lock release"),FRPGTradeIntentJournal::Load(Directory,I.Scope,Loaded)==ERPGTradeJournalRead::Ready);
    TestTrue(TEXT("junction root rejected"),FRPGTradeIntentJournal::Load(Root(TEXT("Linked")),I.Scope,Loaded)==ERPGTradeJournalRead::Unavailable);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradeIntentRestartTest,"ProjectRPG.Trading.Journal.Restart",RPGTradeJournalTests::Flags)
bool FTradeIntentRestartTest::RunTest(const FString&)
{
    using namespace RPGTradeJournalTests;const auto Directory=Root(TEXT("Restart"));FString Phase;FParse::Value(FCommandLine::Get(),TEXT("TradeJournalPhase="),Phase);
    if(Directory.IsEmpty())return false;
    if(Phase==TEXT("write"))
    {
        const auto I=Intent();TestTrue(TEXT("writer persists original command"),FRPGTradeIntentJournal::Create(Directory,I));
        TestTrue(TEXT("write expected original body"),FFileHelper::SaveStringToFile(I.Request.Body,*(Directory/TEXT("Expected.body")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));return true;
    }
    if(!TestEqual(TEXT("separate reader phase required"),Phase,FString(TEXT("read"))))return false;
    FRPGTradeIntentScope Scope;FRPGTradeIntentScope::Create(Endpoint,Account,A,Scope);FRPGTradePendingIntent Loaded;
    if(!TestTrue(TEXT("second UE process loads original"),FRPGTradeIntentJournal::Load(Directory,Scope,Loaded)==ERPGTradeJournalRead::Ready))return false;
    FString Expected;FFileHelper::LoadFileToString(Expected,*(Directory/TEXT("Expected.body")));
    TestEqual(TEXT("original writer bytes survived process exit"),Loaded.Request.Body,Expected);auto C=Client();
    TestTrue(TEXT("reader restores as uncertain"),C.RestorePending(Loaded));TestTrue(TEXT("new process has no inferred session"),C.GetSession()==nullptr);
    FRPGTradeTicket Ticket;TestTrue(TEXT("reader can retry original"),C.BeginAttempt(Ticket));
    TestEqual(TEXT("reader retains original request ID"),C.GetPending()->RequestId,Loaded.Request.RequestId);
    TestEqual(TEXT("reader retains original route"),C.GetPending()->RelativePath,Loaded.Request.RelativePath);
    TestEqual(TEXT("reader retains original body"),C.GetPending()->Body,Expected);
    const FString Receipt=TEXT("{\"requestId\":\"")+Id(Loaded.Request.RequestId)+TEXT("\",\"session\":")+Session()+TEXT(",\"committedAt\":\"2026-11-10T00:00:00Z\",\"rejection\":null}");
    TestTrue(TEXT("reader validates matching receipt"),C.CompleteAttempt(Ticket,200,Receipt)==ERPGTradeResponse::Succeeded);
    TestTrue(TEXT("compare clear after caller confirms result"),FRPGTradeIntentJournal::Clear(Directory,Loaded));return true;
}
#endif
