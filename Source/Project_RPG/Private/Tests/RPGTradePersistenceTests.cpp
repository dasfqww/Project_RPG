#if WITH_DEV_AUTOMATION_TESTS
#include "Trading/RPGTradeLobbyOwner.h"
#include "Trading/RPGTradeIntentJournal.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Async/TaskGraphInterfaces.h"

namespace RPGTradePersistenceTests
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
const FGuid A(1,1,1,1),B(2,2,2,2);
const FString Account=TEXT("76561198000000001");
FString Id(FGuid G){return G.ToString(EGuidFormats::DigitsWithHyphensLower);}
FString Root(const TCHAR* Case){FString R;FParse::Value(FCommandLine::Get(),TEXT("TradePersistenceRoot="),R);return R.IsEmpty()?R:R/Case;}
FRPGTradeIntentScope Scope(const FString& Endpoint=TEXT("http://localhost:3000/api"))
{FRPGTradeIntentScope S;FRPGTradeIntentScope::Create(Endpoint,Account,A,S);return S;}
FString LockPath(const FString& R,const FRPGTradeIntentScope& S)
{FString Key;FRPGTradeIntentJournal::ScopeKey(S,Key);return R/(Key+TEXT(".lock"));}
struct FFake final:IRPGTradeTransport
{
    FString Endpoint=TEXT("http://localhost:3000/api"),Directory,Body;bool Token=true,DurableAtSend=true;
    int32 Posts=0,Gets=0;TFunction<void(FRPGTradeHttpResult)> Done;
    bool GetContext(FString& E,FString& U)const override{E=Endpoint;U=Account;return true;}
    bool CanSend()const override{return Token;}
    void Cancel()override{Done={};}
    bool Send(FGuid,FGuid,const FString& Action,const FString& Data,TFunction<void(FRPGTradeHttpResult)> Callback)override
    {
        if(Action.IsEmpty())++Gets;else
        {
            ++Posts;FRPGTradePendingIntent I;
            DurableAtSend &= FRPGTradeIntentJournal::Load(Directory,Scope(Endpoint),I)==ERPGTradeJournalRead::Ready && I.Request.Body==Data;
        }
        Body=Data;Done=MoveTemp(Callback);return true;
    }
    void Reply(int32 Code,const FString& Data)
    {auto Callback=MoveTemp(Done);if(Callback)Callback({true,Code!=0,Code,Data,0});}
};
FString Receipt(const FRPGTradePendingIntent& I)
{
    return FString::Printf(TEXT(R"({"requestId":"%s","committedAt":"2026-11-10T00:00:00Z","rejection":null,"session":{"tradeId":"%s","initiatorCharacterId":"%s","inviteeCharacterId":"%s","currencyCode":"Gold","revision":"1","offerRevision":"1","expiresAt":"2030-01-01T00:00:00Z","status":"Invited","completed":false,"initiatorOffer":{"currencyAmount":"0","items":[]},"inviteeOffer":{"currencyAmount":"0","items":[]},"initiatorReady":false,"inviteeReady":false,"initiatorConfirmed":false,"inviteeConfirmed":false}})"),*Id(I.Request.RequestId),*Id(I.Request.TradeId),*Id(A),*Id(B));
}
bool Wait(TFunction<bool()> Complete)
{
    const double Deadline=FPlatformTime::Seconds()+15;
    while(!Complete() && FPlatformTime::Seconds()<Deadline)
    {FHttpModule::Get().GetHttpManager().Tick(.01f);FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);FPlatformProcess::Sleep(.01f);}
    return Complete();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceBarrierTest,"ProjectRPG.Trading.Persistence.SaveBarrier",RPGTradePersistenceTests::Flags)
bool FTradePersistenceBarrierTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;const FString R=Root(TEXT("Barrier"));if(R.IsEmpty())return false;
    auto F=MakeShared<FFake>();F->Directory=R;TWeakPtr<FRPGTradeCoordinator> Weak;
    bool Arm=false;TUniquePtr<IFileHandle> Lock;
    auto C=MakeShared<FRPGTradeCoordinator>(F,[&]{auto Current=Weak.Pin();if(Arm && Current && Current->GetClient().GetPending() && !Lock)Lock.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*LockPath(R,Scope()),false,false));return 100.;},R);Weak=C;
    C->SelectCharacter(A);Arm=true;TestTrue(TEXT("retain prepared intent when saving is blocked"),C->Invite(B));Arm=false;
    TestTrue(TEXT("actual lock held during first save"),!!Lock);TestEqual(TEXT("no POST before durable save"),F->Posts,0);
    TestTrue(TEXT("storage blocks replacement commands"),C->IsRecoveryBlocked());
    if(!TestNotNull(TEXT("original prepared request retained"),C->GetClient().GetPending()))return false;
    const auto Original=*C->GetClient().GetPending();TestFalse(TEXT("new invite blocked"),C->Invite(B));
    Lock.Reset();TestTrue(TEXT("retry persists and sends original"),C->Retry());
    TestEqual(TEXT("one original POST"),F->Posts,1);TestEqual(TEXT("body unchanged"),F->Body,Original.Body);
    TestTrue(TEXT("journal exists before transport callback"),F->DurableAtSend);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceRestoreTest,"ProjectRPG.Trading.Persistence.RestoreAndContext",RPGTradePersistenceTests::Flags)
bool FTradePersistenceRestoreTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;const auto R=Root(TEXT("Restore"));if(R.IsEmpty())return false;
    auto F=MakeShared<FFake>();F->Directory=R;auto C=MakeShared<FRPGTradeCoordinator>(F,TFunction<double()>(),R);
    C->SelectCharacter(A);C->Invite(B);FRPGTradePendingIntent I;FRPGTradeIntentJournal::Load(R,Scope(),I);
    auto Late=F->Done;C->Reset();C->SelectCharacter(A);TestTrue(TEXT("restore is unknown"),C->GetClient().GetPending() && C->GetClient().GetPending()->Delivery==ERPGTradeDelivery::Unknown);
    if(Late)Late({true,true,200,Receipt(I),0});TestNotNull(TEXT("previous operation cannot clear restored intent"),C->GetClient().GetPending());
    F->Token=false;C->Pump();TestEqual(TEXT("wait for same player auth"),F->Posts,1);
    F->Token=true;C->Pump();TestEqual(TEXT("original replay sent"),F->Posts,2);TestEqual(TEXT("same command bytes"),F->Body,I.Request.Body);
    F->Reply(200,Receipt(I));TestNull(TEXT("receipt resolves memory intent"),C->GetClient().GetPending());
    FRPGTradePendingIntent Loaded;TestTrue(TEXT("receipt compare-clears durable intent"),FRPGTradeIntentJournal::Load(R,Scope(),Loaded)==ERPGTradeJournalRead::Missing);
    TestFalse(TEXT("receipt cleanup did not block"),C->IsRecoveryBlocked());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceClearTest,"ProjectRPG.Trading.Persistence.ClearBarrier",RPGTradePersistenceTests::Flags)
bool FTradePersistenceClearTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;const auto R=Root(TEXT("Clear"));if(R.IsEmpty())return false;
    auto F=MakeShared<FFake>();F->Directory=R;auto C=MakeShared<FRPGTradeCoordinator>(F,TFunction<double()>(),R);
    C->SelectCharacter(A);C->Invite(B);FRPGTradePendingIntent I;FRPGTradeIntentJournal::Load(R,Scope(),I);
    TUniquePtr<IFileHandle> Lock(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*LockPath(R,Scope()),false,false));
    F->Reply(200,Receipt(I));TestNull(TEXT("trusted receipt retained in memory"),C->GetClient().GetPending());
    TestTrue(TEXT("failed deletion still blocks new commands"),C->IsRecoveryBlocked());
    TestFalse(TEXT("cannot clear closed UI state while storage blocked"),C->ClearClosedSession());
    C->Retry();TestEqual(TEXT("cleanup retry does not replay confirmed POST"),F->Posts,1);
    Lock.Reset();TestTrue(TEXT("explicit cleanup retry succeeds"),C->Retry());
    TestFalse(TEXT("ready after compare clear"),C->IsRecoveryBlocked());TestEqual(TEXT("still only one POST"),F->Posts,1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceCorruptTest,"ProjectRPG.Trading.Persistence.CorruptAndConflict",RPGTradePersistenceTests::Flags)
bool FTradePersistenceCorruptTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;const auto R=Root(TEXT("Corrupt"));if(R.IsEmpty())return false;
    auto F=MakeShared<FFake>();F->Directory=R;auto C=MakeShared<FRPGTradeCoordinator>(F,TFunction<double()>(),R);C->SelectCharacter(A);
    FString Key;FRPGTradeIntentJournal::ScopeKey(Scope(),Key);const FString File=R/(Key+TEXT(".json"));
    FFileHelper::SaveStringToFile(TEXT("corrupt-original"),*File);C->Reset();C->SelectCharacter(A);
    TestTrue(TEXT("invalid existing record blocks"),C->IsRecoveryBlocked());TestFalse(TEXT("no new command"),C->Invite(B));C->Pump();
    TestEqual(TEXT("no network writes"),F->Posts,0);TestFalse(TEXT("search unavailable"),C->CanSearchTargets());
    FString After;FFileHelper::LoadFileToString(After,*File);TestEqual(TEXT("corrupt record retained"),After,FString(TEXT("corrupt-original")));
    // Fixture repair by its owner. Product recovery never deletes a corrupt file.
    FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*File);TestTrue(TEXT("manual storage recovery"),C->Retry());
    TestTrue(TEXT("new invite can now prepare"),C->Invite(B));FRPGTradePendingIntent Old;FRPGTradeIntentJournal::Load(R,Scope(),Old);
    F->Reply(200,Receipt(Old));C->Reset();C->SelectCharacter(A);
    auto Other=MakeShared<FRPGTradeCoordinator>(F,TFunction<double()>(),R);F->Token=false;Other->SelectCharacter(A);Other->Invite(B);
    TestFalse(TEXT("another process intent cannot be overwritten"),C->Invite(B));
    TestNotNull(TEXT("previously missing intent discovered before new command"),C->GetClient().GetPending());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceOwnerTest,"ProjectRPG.Trading.Persistence.LobbyGates",RPGTradePersistenceTests::Flags)
bool FTradePersistenceOwnerTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;const auto R=Root(TEXT("Lobby"));if(R.IsEmpty())return false;
    auto F=MakeShared<FFake>();F->Directory=R;F->Token=false;TSharedPtr<FRPGTradeCoordinator> C=MakeShared<FRPGTradeCoordinator>(F,TFunction<double()>(),R);C->SelectCharacter(A);C->Invite(B);C.Reset();
    FRPGTradeLobbyContext Frame{F->Endpoint,Account,A,false,true,true,false};
    auto O=FRPGTradeLobbyOwner::Create(F,[&]{return Frame;},{},R);O->Tick();
    TestTrue(TEXT("recovery discovered before normal trading eligibility"),O->GetClient().GetPending()!=nullptr);
    TestTrue(TEXT("disabled feature still exposes existing recovery"),O->IsAvailable());TestTrue(TEXT("pending recovery blocks travel"),O->BlocksTransitions());
    TestEqual(TEXT("no POST without token"),F->Posts,0);O->Shutdown();
    // A clean, disabled feature does not poll backend or block an unrelated lobby.
    auto Clean=MakeShared<FFake>();Clean->Directory=Root(TEXT("CleanLobby"));Frame.bEligible=true;
    O=FRPGTradeLobbyOwner::Create(Clean,[&]{return Frame;},{},Clean->Directory);O->Tick();
    TestFalse(TEXT("disabled clean lobby stays unavailable"),O->IsAvailable());TestFalse(TEXT("clean lobby not blocked"),O->BlocksTransitions());
    TestEqual(TEXT("no speculative active read with feature disabled"),Clean->Gets,0);O->Shutdown();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTradePersistenceHttpRestartTest,"ProjectRPG.Trading.Persistence.HttpRestart",RPGTradePersistenceTests::Flags)
bool FTradePersistenceHttpRestartTest::RunTest(const FString&)
{
    using namespace RPGTradePersistenceTests;FString Endpoint,Phase;FParse::Value(FCommandLine::Get(),TEXT("TradePersistenceEndpoint="),Endpoint);FParse::Value(FCommandLine::Get(),TEXT("TradePersistencePhase="),Phase);
    if(Endpoint.IsEmpty() || Root(TEXT("Http")).IsEmpty())return false;
    bool Token=true;auto Transport=MakeShared<FRPGHttpTradeTransport>([&](FRPGTradeHttpContext& Out){Out={Endpoint,Account,Token?TEXT("persistence-fixture-player"):TEXT("")};return true;},[&](const FRPGTradeHttpContext& Captured,int32){return Captured.Endpoint==Endpoint && Captured.Account==Account;},5.f);
    auto C=MakeShared<FRPGTradeCoordinator>(Transport,TFunction<double()>(),Root(TEXT("Http")));C->SelectCharacter(A);
    if(Phase==TEXT("write"))
    {
        TestTrue(TEXT("first process sends saved invite"),C->Invite(B));
        TestTrue(TEXT("committed response replaced by 503 remains unknown"),Wait([&]{return C->GetClient().GetPending() && C->GetClient().GetPending()->Delivery==ERPGTradeDelivery::Unknown;}));
        FRPGTradePendingIntent Saved;TestTrue(TEXT("unresolved HTTP command retained on disk"),FRPGTradeIntentJournal::Load(Root(TEXT("Http")),Scope(Endpoint),Saved)==ERPGTradeJournalRead::Ready);
        FString Raw,Key;FRPGTradeIntentJournal::ScopeKey(Scope(Endpoint),Key);FFileHelper::LoadFileToString(Raw,*(Root(TEXT("Http"))/(Key+TEXT(".json"))));TestFalse(TEXT("credential not persisted"),Raw.Contains(TEXT("persistence-fixture-player")));return true;
    }
    if(!TestEqual(TEXT("second process phase"),Phase,FString(TEXT("read"))))return false;
    if(!TestNotNull(TEXT("second process automatically restored original"),C->GetClient().GetPending()))return false;
    const auto Original=*C->GetClient().GetPending();C->Pump();
    TestTrue(TEXT("real HTTP receipt replay and durable cleanup"),Wait([&]{return !C->GetClient().GetPending() && !C->IsRecoveryBlocked();}));
    TestTrue(TEXT("server session from matching original receipt"),C->GetClient().GetSession() && C->GetClient().GetSession()->TradeId==Original.TradeId);
    FRPGTradePendingIntent Saved;TestTrue(TEXT("durable path removed after matching receipt"),FRPGTradeIntentJournal::Load(Root(TEXT("Http")),Scope(Endpoint),Saved)==ERPGTradeJournalRead::Missing);return true;
}
#endif
