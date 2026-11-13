#include "Riding/RPGRidingCommandCoordinator.h"
#include "Riding/RPGRidingIntentJson.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& S) { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O) { FString S; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S; }
    TSharedPtr<FJsonObject> Fixture()
    { FString S; FFileHelper::LoadFileToString(S, *FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE"))); auto F = Object(S);
      return F && F->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? F : nullptr; }
    FString Root(const TCHAR* Name) { FString S; FParse::Value(FCommandLine::Get(),TEXT("RidingIntentRoot="),S); return S.IsEmpty()?S:S/Name; }
    FRPGRidingPendingIntent Intent(bool bPreferences=false, bool bSeed=true)
    {
        auto F=Fixture(); FRPGRidingPendingIntent I; if(!F)return I; FGuid C;
        FGuid::Parse(F->GetStringField(bSeed?TEXT("seedCharacter"):TEXT("character")),C);
        FRPGRidingIntentScope::Create(F->GetStringField(TEXT("endpoint")),F->GetStringField(TEXT("account")),C,I.Scope);
        I.Body=F->GetStringField(bSeed?(bPreferences?TEXT("seedPreferencesBody"):TEXT("seedRegistrationBody")):(bPreferences?TEXT("preferencesBody"):TEXT("registrationBody")));
        FGuid::Parse(Object(I.Body)->GetStringField(TEXT("requestId")),I.RequestId);I.Operation=bPreferences?ERPGRidingPersistentOperation::Preferences:ERPGRidingPersistentOperation::Register;return I;
    }
    FString Receipt(bool bPreferences=false) { auto F=Fixture();return F?F->GetStringField(bPreferences?TEXT("preferences"):TEXT("registration")):FString(); }
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCommandContext* C;FString Directory;FCompletion Done;TArray<FRPGRidingPendingIntent> Sent;bool bAuth=true,bPersisted=true;
        FFake(FRPGRidingCommandContext& In,FString R):C(&In),Directory(MoveTemp(R)){}
        virtual bool GetContext(FString& E,FString& A)const override{E=C->Scope.Endpoint;A=C->Scope.Account;return true;}
        virtual bool HasCredential()const override{return bAuth;}virtual bool CanSend()const override{return bAuth&&!Done;}
        virtual bool ReadCollection(FGuid,FCompletion)override{return false;}
        virtual bool SendIntent(const FRPGRidingPendingIntent& I,FCompletion F)override
        {FRPGRidingPendingIntent Saved;bPersisted&=FRPGRidingIntentJournal::Load(Directory,I.Scope,Saved)==ERPGRidingJournalRead::Ready&&Saved==I;Sent.Add(I);Done=MoveTemp(F);return true;}
        virtual bool ReadReceipt(const FRPGRidingPendingIntent&,FCompletion F)override{Done=MoveTemp(F);return true;}
        virtual void Cancel()override{Done={};}
        void Reply(int32 Code,const FString& Body=FString(),double Delay=0){auto F=MoveTemp(Done);if(F)F({true,Code!=0,Code,Body,Delay});}
    };
    struct FOwner
    {
        FRPGRidingCommandContext C;TSharedRef<FFake> T;TSharedPtr<FRPGRidingCommandCoordinator> Owner;
        explicit FOwner(const TCHAR* Name):C{Intent().Scope,true},T(MakeShared<FFake>(C,Root(Name)))
        {Owner=MakeShared<FRPGRidingCommandCoordinator>(T,[this]{return C;},Root(Name));}
        ~FOwner(){Owner->Shutdown();}
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandReceipts,"ProjectRPG.Riding.Command.Receipts",Flags)
bool FRidingCommandReceipts::RunTest(const FString&)
{
    FRPGRidingReceipt R;TestTrue(TEXT("Real Registered envelope"),RPGRidingReceipt::Decode(Intent(),Receipt(),false,R)&&R.bConsumedItem);
    auto O=Object(Receipt());TestTrue(TEXT("Real recovery shape"),RPGRidingReceipt::Decode(Intent(),Json(O->GetObjectField(TEXT("receipt"))),true,R));
    TestTrue(TEXT("Real settings envelope"),RPGRidingReceipt::Decode(Intent(true),Receipt(true),false,R)&&R.Revision==1);
    O=Object(Receipt());O->SetStringField(TEXT("outcome"),TEXT("AlreadyApplied"));TestTrue(TEXT("Replay envelope"),RPGRidingReceipt::Decode(Intent(),Json(O),false,R));
    O->SetStringField(TEXT("outcome"),TEXT("AlreadyOwned"));O->GetObjectField(TEXT("receipt"))->SetStringField(TEXT("outcome"),TEXT("AlreadyOwned"));
    O->GetObjectField(TEXT("receipt"))->SetField(TEXT("itemReceipt"),MakeShared<FJsonValueNull>());
    TestTrue(TEXT("No consumption for AlreadyOwned"),RPGRidingReceipt::Decode(Intent(),Json(O),false,R)&&!R.bConsumedItem);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandMalformed,"ProjectRPG.Riding.Command.Malformed",Flags)
bool FRidingCommandMalformed::RunTest(const FString&)
{
    auto Reject=[&](const FString& Body,bool Pref=false){FRPGRidingReceipt R;R.RequestId=FGuid::NewGuid();TestFalse(TEXT("Mismatching proof rejected"),RPGRidingReceipt::Decode(Intent(Pref),Body,false,R));TestFalse(TEXT("Output cleared"),R.RequestId.IsValid());};
    for(const auto* Key:{TEXT("requestId"),TEXT("characterId"),TEXT("operation")}){auto O=Object(Receipt());O->GetObjectField(TEXT("receipt"))->SetStringField(Key,TEXT("wrong"));Reject(Json(O));}
    auto O=Object(Receipt());O->GetObjectField(TEXT("receipt"))->GetObjectField(TEXT("itemReceipt"))->SetStringField(TEXT("requestId"),FGuid::NewGuid().ToString());Reject(Json(O));
    O=Object(Receipt(true));O->GetObjectField(TEXT("receipt"))->SetNumberField(TEXT("preferencesRevision"),1);Reject(Json(O),true);
    O=Object(Receipt(true));O->GetObjectField(TEXT("receipt"))->GetObjectField(TEXT("preferences"))->SetStringField(TEXT("selectedMountId"),TEXT("wolf"));Reject(Json(O),true);
    Reject(Receipt().LeftChop(1)+TEXT(",\"outcome\":\"Registered\"}"));Reject(TEXT("null"));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandUnknown,"ProjectRPG.Riding.Command.Unknown",Flags)
bool FRidingCommandUnknown::RunTest(const FString&)
{
    FOwner F(TEXT("Unknown"));const auto I=Intent();TestTrue(TEXT("Persist then dispatch"),F.Owner->Submit(I)&&F.T->bPersisted);
    F.T->Reply(0);TestTrue(TEXT("Uncertain keeps original and blocks transitions"),F.Owner->GetPending()&&F.Owner->BlocksTransitions());
    TestTrue(TEXT("Explicit retry"),F.Owner->Retry());TestTrue(TEXT("Exact bytes/UUID"),F.T->Sent.Num()==2&&F.T->Sent[0]==F.T->Sent[1]);
    F.T->Reply(409,TEXT("{\"error\":\"RevisionConflict\"}"));TestNotNull(TEXT("Conflict after uncertainty is not final"),F.Owner->GetPending());
    F.Owner->Recover();F.T->Reply(404,TEXT("{\"error\":\"NotFound\"}"));TestNotNull(TEXT("Recovery 404 is not final"),F.Owner->GetPending());
    F.Owner->Recover();F.T->Reply(200,Json(Object(Receipt())->GetObjectField(TEXT("receipt"))));
    TestTrue(TEXT("Correlated proof commits and requires fresh reads"),F.Owner->GetState()==ERPGRidingCommandState::Committed&&F.Owner->NeedsFreshReads());
    TestFalse(TEXT("New command blocked until fresh reads"),F.Owner->Submit(Intent(true)));
    TestTrue(TEXT("Explicit fresh read acknowledgement"),F.Owner->AcknowledgeFreshReads(I.RequestId));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandContext,"ProjectRPG.Riding.Command.Context",Flags)
bool FRidingCommandContext::RunTest(const FString&)
{
    FOwner F(TEXT("Context"));auto I=Intent();F.Owner->Submit(I);auto Late=MoveTemp(F.T->Done);const auto Original=F.C.Scope;
    F.C.Scope.Character=FGuid::NewGuid();F.Owner->RefreshContext();Late({true,true,200,Receipt(),0});TestNull(TEXT("Old result cannot cross character"),F.Owner->GetReceipt());
    F.C.Scope=Original;F.Owner->RefreshContext();TestNotNull(TEXT("Returning restores uncertain pending"),F.Owner->GetPending());
    F.T->bAuth=false;TestNull(TEXT("Auth loss hides pending details"),F.Owner->GetPending());TestTrue(TEXT("Unresolved work remains blocked"),F.Owner->BlocksTransitions());
    F.T->bAuth=true;F.Owner->Recover();F.T->Reply(200,Json(Object(Receipt())->GetObjectField(TEXT("receipt"))));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandStorage,"ProjectRPG.Riding.Command.Storage",Flags)
bool FRidingCommandStorage::RunTest(const FString&)
{
    FOwner F(TEXT("Storage"));F.Owner->Submit(Intent());F.T->Reply(409,TEXT("{\"error\":\"RevisionConflict\"}"));
    TestTrue(TEXT("Definitive first refusal clears"),F.Owner->GetState()==ERPGRidingCommandState::Rejected&&!F.Owner->GetPending());
    F.Owner->Submit(Intent());F.T->Reply(503,TEXT("{}"),30);TestFalse(TEXT("Retry-after enforced"),F.Owner->Retry());
    TestNotNull(TEXT("503 pending preserved"),F.Owner->GetPending());
    FOwner Locked(TEXT("ClearStorage")); const auto I=Intent(); Locked.Owner->Submit(I); FString Key;
    TestTrue(TEXT("Storage scope key"),FRPGRidingIntentJournal::ScopeKey(I.Scope,Key));
    TUniquePtr<IFileHandle> Held(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(Root(TEXT("ClearStorage"))/(Key+TEXT(".lock"))),false,false));
    TestTrue(TEXT("Hold journal lock before proof"),Held.IsValid()); Locked.T->Reply(200,Receipt());
    TestTrue(TEXT("Clear failure retains pending and blocks all dispatch"),Locked.Owner->GetState()==ERPGRidingCommandState::StorageBlocked&&Locked.Owner->GetPending()&&Locked.Owner->BlocksTransitions()&&!Locked.Owner->Retry());
    Held.Reset(); TestTrue(TEXT("Retry only proven clear after lock release"),Locked.Owner->RetryStorage());
    TestTrue(TEXT("Receipt preserved without resend and fresh reads still required"),Locked.Owner->GetReceipt()&&Locked.Owner->NeedsFreshReads()&&!Locked.Owner->GetPending()&&Locked.T->Sent.Num()==1);
    return true;
}
namespace
{
    struct FRun
    {
        FAutomationTestBase* Test;FRPGRidingCommandContext Context;TSharedPtr<FRPGHttpRidingOwnerTransport> T;TSharedPtr<FRPGRidingCommandCoordinator> Owner;double Until=0;int32 Step=0;bool Reader=false;
        ~FRun(){if(Owner)Owner->Shutdown();}
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingCommandWait,TSharedPtr<FRun>,Run);
    bool FRidingCommandWait::Update()
    {
        const auto State=Run->Owner->GetState();if(State==ERPGRidingCommandState::AwaitingServer&&FPlatformTime::Seconds()<Run->Until)return false;
        if(!Run->Reader){Run->Test->TestEqual(TEXT("Actual commit followed by 503 remains uncertain"),State,ERPGRidingCommandState::RecoveryRequired);Run->Test->TestNotNull(TEXT("Original retained across exit"),Run->Owner->GetPending());return true;}
        Run->Test->TestEqual(TEXT("Actual correlated receipt resolves"),State,ERPGRidingCommandState::Committed);
        if(Run->Step++==0)
        {
            Run->Test->TestTrue(TEXT("Registration requires current assets"),Run->Owner->NeedsFreshReads());
            // This fixture is the owner of the isolated actor; production caller must perform current collection/inventory reads.
            Run->Owner->AcknowledgeFreshReads(Intent(false,false).RequestId);
            Run->Test->TestTrue(TEXT("Actual settings PUT"),Run->Owner->Submit(Intent(true,false)));Run->Until=FPlatformTime::Seconds()+15;return false;
        }
        Run->Test->TestTrue(TEXT("Settings receipt requires refresh too"),Run->Owner->NeedsFreshReads());return true;
    }
    TSharedPtr<FRun> Http(FAutomationTestBase* Test,bool Reader)
    {
        auto F=Fixture();if(!F)return nullptr;auto R=MakeShared<FRun>();R->Test=Test;R->Reader=Reader;R->Context={Intent(false,false).Scope,true};const TWeakPtr<FRun> Weak=R;
        const FString Token=TEXT("Bearer ")+F->GetStringField(TEXT("token"));
        R->T=MakeShared<FRPGHttpRidingOwnerTransport>([Weak,Token](FRPGRidingOwnerHttpContext& C){auto R=Weak.Pin();if(!R)return false;C={R->Context.Scope.Endpoint,R->Context.Scope.Account,Token};return true;},
          [Weak,Token](const FRPGRidingOwnerHttpContext& C,int32){auto R=Weak.Pin();return R&&C.Endpoint==R->Context.Scope.Endpoint&&C.Account==R->Context.Scope.Account&&C.Authorization==Token;});
        R->Owner=MakeShared<FRPGRidingCommandCoordinator>(R->T.ToSharedRef(),[Weak]{auto R=Weak.Pin();return R?R->Context:FRPGRidingCommandContext{};},Root(TEXT("Restart")));
        R->Until=FPlatformTime::Seconds()+15;return R;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandWriter,"ProjectRPG.Riding.Command.MemoryHttpWriter",Flags)
bool FRidingCommandWriter::RunTest(const FString&)
{auto R=Http(this,false);if(!R)return false;TestTrue(TEXT("Actual registration POST"),R->Owner->Submit(Intent(false,false)));ADD_LATENT_AUTOMATION_COMMAND(FRidingCommandWait(R));return true;}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCommandReader,"ProjectRPG.Riding.CommandRestart.Reader",Flags)
bool FRidingCommandReader::RunTest(const FString&)
{auto R=Http(this,true);if(!R)return false;TestNotNull(TEXT("New process restores original"),R->Owner->GetPending());TestTrue(TEXT("Actual receipt GET"),R->Owner->Recover());ADD_LATENT_AUTOMATION_COMMAND(FRidingCommandWait(R));return true;}
#endif
