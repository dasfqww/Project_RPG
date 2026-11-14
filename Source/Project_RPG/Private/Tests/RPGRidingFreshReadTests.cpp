#include "Riding/RPGRidingFreshRead.h"
#include "Riding/RPGRidingCollectionReader.h"
#include "Riding/RPGRidingIntentJson.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& S) { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O) { FString S; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&S)); return S; }
    TSharedPtr<FJsonObject> Fixture()
    {
        FString S; FFileHelper::LoadFileToString(S, *FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE")));
        auto F = Object(S); return F && F->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? F : nullptr;
    }
    FString Root() { FString S; FParse::Value(FCommandLine::Get(), TEXT("RidingFreshRoot="), S); return S.IsEmpty() ? S : S / FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    FRPGRidingPendingIntent Intent(bool Actual = false)
    {
        FRPGRidingPendingIntent I; auto F = Fixture(); if (!F) return I; FGuid Character;
        FGuid::Parse(F->GetStringField(Actual ? TEXT("character") : TEXT("seedCharacter")), Character);
        FRPGRidingIntentScope::Create(F->GetStringField(TEXT("endpoint")), F->GetStringField(Actual ? TEXT("account") : TEXT("seedAccount")), Character, I.Scope);
        I.Body = F->GetStringField(Actual ? TEXT("registrationBody") : TEXT("seedBody"));
        FGuid::Parse(Object(I.Body)->GetStringField(TEXT("requestId")), I.RequestId); return I;
    }
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCommandContext* C; FCompletion Done; int32 Writes = 0, Collections = 0, Inventories = 0;
        explicit FFake(FRPGRidingCommandContext& In) : C(&In) {}
        virtual bool GetContext(FString& E, FString& A) const override { E=C->Scope.Endpoint; A=C->Scope.Account; return true; }
        virtual bool HasCredential() const override { return C->bEligible; }
        virtual bool CanSend() const override { return HasCredential() && !Done; }
        virtual bool SendIntent(const FRPGRidingPendingIntent&, FCompletion F) override { ++Writes; Done=MoveTemp(F); return true; }
        virtual bool ReadCollection(FGuid, FCompletion F) override { ++Collections; Done=MoveTemp(F); return true; }
        virtual bool ReadInventory(FGuid, FCompletion F) override { ++Inventories; Done=MoveTemp(F); return true; }
        virtual void Cancel() override { Done={}; }
        void Reply(int32 Code, const FString& Body, double Delay=0) { auto F=MoveTemp(Done); if(F)F({true,true,Code,Body,Delay}); }
    };
    struct FSink
    {
        uint64 Generation=0, Ticket=0; int32 Applied=0, Installed=0, Items=-1, TargetQuantity=-1; FGuid Target;
        TFunction<void()> OnInventory, OnCollection;
        FRPGRidingFreshRead::FSink Bind()
        {
            return {[this](FGuid){Ticket=++Generation;return true;},[this]{return Ticket==Generation;},
                [this](const TArray<FRPGInventoryProjectionEntry>& I,const TArray<FRPGInventoryProjectionEntry>&){++Applied;Items=I.Num();TargetQuantity=0;for(const auto& E:I)if(E.GetItemId()==Target)TargetQuantity=E.GetQuantity();Ticket=++Generation;if(OnInventory)OnInventory();return true;},
                [this](const FRPGRidingCollection&){++Installed;if(OnCollection)OnCollection();return true;}};
        }
    };
    struct FOwner
    {
        FRPGRidingCommandContext C{Intent().Scope,true}; FSink Sink;
        TSharedRef<FFake> CommandTransport=MakeShared<FFake>(C), Reads=MakeShared<FFake>(C);
        TSharedPtr<FRPGRidingCommandCoordinator> Commands;
        TSharedPtr<FRPGRidingFreshRead> Fresh;
        FOwner()
        {
            FGuid::Parse(Object(Intent().Body)->GetStringField(TEXT("itemId")),Sink.Target);
            Commands=MakeShared<FRPGRidingCommandCoordinator>(CommandTransport,[this]{return C;},Root());
            Commands->Submit(Intent()); CommandTransport->Reply(200,Fixture()->GetStringField(TEXT("receipt")));
            Fresh=MakeShared<FRPGRidingFreshRead>(Commands.ToSharedRef(),Reads,[this]{return C;},Sink.Bind());
        }
        ~FOwner(){Fresh->Shutdown();Commands->Shutdown();}
        void Collection(){Reads->Reply(200,Fixture()->GetStringField(TEXT("collection")));}
        void Items(){Reads->Reply(200,Fixture()->GetStringField(TEXT("items")));}
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshSuccess,"ProjectRPG.Riding.FreshAssets.Success",Flags)
bool FRidingFreshSuccess::RunTest(const FString&)
{
    FOwner F; TestTrue(TEXT("Receipt alone holds barrier"),F.Commands->NeedsFreshReads()&&F.Commands->BlocksTransitions());
    TestTrue(TEXT("Fresh request starts"),F.Fresh->Refresh()); TestEqual(TEXT("Collection first"),F.Reads->Collections,1);
    TestEqual(TEXT("No inventory before collection"),F.Reads->Inventories,0); F.Collection();
    TestTrue(TEXT("Partial success retains barrier"),F.Commands->BlocksTransitions()); TestEqual(TEXT("Inventory second"),F.Reads->Inventories,1);
    F.Items(); TestTrue(TEXT("Both current snapshots release barrier"),F.Fresh->GetState()==ERPGRidingFreshReadState::Ready&&!F.Commands->BlocksTransitions());
    TestTrue(TEXT("Current projection applied before release"),F.Sink.Applied==1&&F.Sink.Installed==1&&F.Sink.TargetQuantity==1);
    TestEqual(TEXT("Refresh never writes"),F.Reads->Writes,0); TestFalse(TEXT("Settled receipt cannot start another sequence"),F.Fresh->Refresh()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshFailures,"ProjectRPG.Riding.FreshAssets.Failures",Flags)
bool FRidingFreshFailures::RunTest(const FString&)
{
    { FOwner F;F.Fresh->Refresh();F.Reads->Reply(503,TEXT("{}"),30);TestFalse(TEXT("Retry-after prevents dispatch"),F.Fresh->Refresh());TestFalse(TEXT("No automatic retry eligibility"),F.Fresh->IsUnattempted());TestTrue(TEXT("503 holds barrier"),F.Commands->BlocksTransitions()); }
    for(int32 Mode=0;Mode<4;++Mode)
    {
        FOwner F;F.Fresh->Refresh();F.Collection();FString Body=Fixture()->GetStringField(TEXT("items"));
        if(Mode==0)Body=TEXT("{\"records\":[],\"records\":[]}");
        else if(Mode==1){auto O=Object(Body);O->GetArrayField(TEXT("items"))[0]->AsObject()->GetObjectField(TEXT("owner"))->SetStringField(TEXT("ownerId"),FGuid::NewGuid().ToString());Body=Json(O);}
        else if(Mode==2){auto O=Object(Body);auto Value=O->GetArrayField(TEXT("items"))[0];TArray<TSharedPtr<FJsonValue>> Rows;Rows.Init(Value,500);O->SetArrayField(TEXT("items"),Rows);Body=Json(O);}
        F.Reads->Reply(Mode==3?401:200,Body);
        TestTrue(TEXT("Invalid/incomplete/auth response holds barrier"),F.Commands->BlocksTransitions()&&F.Fresh->GetState()!=ERPGRidingFreshReadState::Ready);
        TestEqual(TEXT("Rejected response never imports"),F.Sink.Applied,0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshSuperseded,"ProjectRPG.Riding.FreshAssets.Superseded",Flags)
bool FRidingFreshSuperseded::RunTest(const FString&)
{
    FOwner F;F.Fresh->Refresh();F.Collection();++F.Sink.Generation;F.Items();
    TestTrue(TEXT("Newer local read/write invalidates old snapshot"),F.Commands->BlocksTransitions()&&F.Sink.Applied==0);
    F.Fresh->RefreshContext();TestEqual(TEXT("Context check does not retry"),F.Reads->Collections,1);
    TestTrue(TEXT("Explicit retry claims new generation"),F.Fresh->Refresh());F.Collection();F.Items();TestFalse(TEXT("Fresh retry releases"),F.Commands->BlocksTransitions()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshContext,"ProjectRPG.Riding.FreshAssets.Context",Flags)
bool FRidingFreshContext::RunTest(const FString&)
{
    FOwner F;F.Fresh->Refresh();auto Late=MoveTemp(F.Reads->Done);F.C.bEligible=false;F.Fresh->RefreshContext();
    Late({true,true,200,Fixture()->GetStringField(TEXT("collection")),0});TestEqual(TEXT("Late callback after auth loss ignored"),F.Reads->Inventories,0);
    TestTrue(TEXT("Auth loss preserves command barrier"),F.Commands->BlocksTransitions());F.C.bEligible=true;
    TestFalse(TEXT("Returning auth does not automatically retry"),F.Fresh->IsUnattempted());TestTrue(TEXT("Explicit read after auth restoration"),F.Fresh->Refresh());
    F.Collection();F.C.Scope.Character=FGuid::NewGuid();F.Items();TestEqual(TEXT("Different character cannot receive old inventory"),F.Sink.Applied,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshReentry,"ProjectRPG.Riding.FreshAssets.InstallAndReentry",Flags)
bool FRidingFreshReentry::RunTest(const FString&)
{
    { FOwner F;F.Sink.OnInventory=[&]{++F.Sink.Generation;};F.Fresh->Refresh();F.Collection();F.Items();TestTrue(TEXT("Reentrant newer read blocks acknowledgement"),F.Commands->BlocksTransitions()&&F.Sink.Installed==0); }
    { FOwner F;F.Sink.OnCollection=[&]{F.C.bEligible=false;};F.Fresh->Refresh();F.Collection();F.Items();TestTrue(TEXT("Context loss during publication blocks acknowledgement"),F.Commands->BlocksTransitions()); }
    FOwner F;FRPGRidingCollectionContext C{F.C.Scope.Endpoint,F.C.Scope.Account,F.C.Scope.Character,true};
    auto T=MakeShared<FFake>(F.C);auto Reader=MakeShared<FRPGRidingCollectionReader>(T,[&]{return C;});FRPGRidingCollection Snapshot;
    TestTrue(TEXT("Production collection decoded"),RPGRidingCollection::Decode(Fixture()->GetStringField(TEXT("collection")),C.Character,Snapshot));
    TestTrue(TEXT("Install current read into open screen"),Reader->InstallFreshSnapshot(C,Snapshot));
    auto Older=Snapshot;Older.RosterRevision=0;TestFalse(TEXT("Never roll back a newer screen revision"),Reader->InstallFreshSnapshot(C,Older));
    Reader->Read();TestFalse(TEXT("Never replace a pending newer screen read"),Reader->InstallFreshSnapshot(C,Snapshot));Reader->Shutdown();return true;
}
namespace
{
    struct FHttpRun
    {
        FAutomationTestBase* Test=nullptr;FRPGRidingCommandContext C;FSink Sink;
        TSharedPtr<FRPGRidingCommandCoordinator> Commands;TSharedPtr<FRPGRidingFreshRead> Fresh;double Until=0;int32 Step=0;
        ~FHttpRun(){if(Fresh)Fresh->Shutdown();if(Commands)Commands->Shutdown();}
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingFreshHttpWait,TSharedPtr<FHttpRun>,Run);
    bool FRidingFreshHttpWait::Update()
    {
        if(Run->Step==0)
        {
            if(Run->Commands->GetState()==ERPGRidingCommandState::AwaitingServer&&FPlatformTime::Seconds()<Run->Until)return false;
            Run->Test->TestTrue(TEXT("Actual registration receipt verified"),Run->Commands->GetState()==ERPGRidingCommandState::Committed&&Run->Commands->NeedsFreshReads());
            Run->Test->TestTrue(TEXT("Start actual current collection/items GETs"),Run->Fresh->Refresh());Run->Step=1;Run->Until=FPlatformTime::Seconds()+20;return false;
        }
        const auto S=Run->Fresh->GetState();if((S==ERPGRidingFreshReadState::ReadingCollection||S==ERPGRidingFreshReadState::ReadingInventory)&&FPlatformTime::Seconds()<Run->Until)return false;
        Run->Test->TestTrue(TEXT("Actual fresh reads release barrier"),S==ERPGRidingFreshReadState::Ready&&!Run->Commands->BlocksTransitions());
        Run->Test->TestEqual(TEXT("Later consumption is current; receipt remainder never resurrected"),Run->Sink.TargetQuantity,0);Run->Test->TestTrue(TEXT("Unrelated current starter items retained"),Run->Sink.Items>0);return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingFreshHttp,"ProjectRPG.Riding.FreshAssets.MemoryHttp",Flags)
bool FRidingFreshHttp::RunTest(const FString&)
{
    auto F=Fixture();if(!F)return false;auto R=MakeShared<FHttpRun>();R->Test=this;R->C={Intent(true).Scope,true};R->Until=FPlatformTime::Seconds()+20;
    FGuid::Parse(Object(Intent(true).Body)->GetStringField(TEXT("itemId")),R->Sink.Target);
    const TWeakPtr<FHttpRun> Weak=R;const FString Token=TEXT("Bearer ")+F->GetStringField(TEXT("token"));
    auto MakeTransport=[Weak,Token]
    {
        return MakeShared<FRPGHttpRidingOwnerTransport>([Weak,Token](FRPGRidingOwnerHttpContext& C){auto S=Weak.Pin();if(!S)return false;C={S->C.Scope.Endpoint,S->C.Scope.Account,Token};return true;},
            [Weak,Token](const FRPGRidingOwnerHttpContext& C,int32){auto S=Weak.Pin();return S&&C.Endpoint==S->C.Scope.Endpoint&&C.Account==S->C.Scope.Account&&C.Authorization==Token;});
    };
    R->Commands=MakeShared<FRPGRidingCommandCoordinator>(MakeTransport(),[Weak]{auto S=Weak.Pin();return S?S->C:FRPGRidingCommandContext{};},Root());
    R->Fresh=MakeShared<FRPGRidingFreshRead>(R->Commands.ToSharedRef(),MakeTransport(),[Weak]{auto S=Weak.Pin();return S?S->C:FRPGRidingCommandContext{};},R->Sink.Bind());
    TestTrue(TEXT("Actual Player POST"),R->Commands->Submit(Intent(true)));ADD_LATENT_AUTOMATION_COMMAND(FRidingFreshHttpWait(R));return true;
}
#endif
