#include "Tests/RPGRidingDashPredictionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingDashIntent.h"
#include "Riding/RPGRidingDashPrediction.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Riding/RPGRidingMovementPrediction.h"
#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/UnrealType.h"
#if UE_WITH_IRIS
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#endif
#include <limits>

ARPGRidingDashPredictionTestPawn::ARPGRidingDashPredictionTestPawn(const FObjectInitializer& I)
    : Super(I.SetDefaultSubobjectClass<URPGRidingDashPredictionTestASC>(TEXT("ASC"))) {}
int32 ARPGRidingDashPredictionTestPawn::GetFunctionCallspace(UFunction* F, FFrame* Stack)
{
#if WITH_DEV_AUTOMATION_TESTS
    const auto Name = F->GetFName();
    if (GetLocalRole() == ROLE_AutonomousProxy && (Name == TEXT("ServerTryActivateAbility")
        || Name == TEXT("ServerSetReplicatedTargetData") || Name == TEXT("ServerSetReplicatedTargetDataCancelled"))) return FunctionCallspace::Local;
#endif
    return Super::GetFunctionCallspace(F, Stack);
}
void URPGRidingDashPredictionTestASC::ServerTryActivateAbility_Implementation(FGameplayAbilitySpecHandle H, bool Input, FPredictionKey K)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!GetOwner()->HasAuthority()) { SentKey = K; ++ActivationRequests; return; }
#endif
    Super::ServerTryActivateAbility_Implementation(H, Input, K);
}
void URPGRidingDashPredictionTestASC::ServerSetReplicatedTargetData_Implementation(FGameplayAbilitySpecHandle H, FPredictionKey K,
    const FGameplayAbilityTargetDataHandle& D, FGameplayTag T, FPredictionKey M)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!GetOwner()->HasAuthority()) { SentData = D; SentMessageKey = M; ++IntentRequests; return; }
#endif
    Super::ServerSetReplicatedTargetData_Implementation(H, K, D, T, M);
}
void URPGRidingDashPredictionTestASC::ServerSetReplicatedTargetDataCancelled_Implementation(FGameplayAbilitySpecHandle H, FPredictionKey K, FPredictionKey M)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (!GetOwner()->HasAuthority()) return;
#endif
    Super::ServerSetReplicatedTargetDataCancelled_Implementation(H, K, M);
}
void URPGRidingDashPredictionTestASC::ClientActivateAbilitySucceed_Implementation(FGameplayAbilitySpecHandle H, FPredictionKey K)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GetOwner()->HasAuthority()) return;
#endif
    Super::ClientActivateAbilitySucceed_Implementation(H, K);
}
void URPGRidingDashPredictionTestASC::ClientActivateAbilityFailed_Implementation(FGameplayAbilitySpecHandle H, int16 K)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GetOwner()->HasAuthority()) return;
#endif
    Super::ClientActivateAbilityFailed_Implementation(H, K);
}
void URPGRidingDashPredictionTestASC::ClientEndAbility_Implementation(FGameplayAbilitySpecHandle H, FGameplayAbilityActivationInfo I)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GetOwner()->HasAuthority()) return;
#endif
    Super::ClientEndAbility_Implementation(H, I);
}
void URPGRidingDashPredictionTestASC::ClientCancelAbility_Implementation(FGameplayAbilitySpecHandle H, FGameplayAbilityActivationInfo I)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (GetOwner()->HasAuthority()) return;
#endif
    Super::ClientCancelAbility_Implementation(H, I);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingDashPredictionTestAccess
{
    static void State(URPGRidingComponent& C, const FRPGRidingRepState& S) { const auto Old=C.State; C.State=S; C.OnRep_State(Old); }
    static void Replica(URPGRidingComponent& C, UAbilitySystemComponent& ASC, const URPGRidingComponent& Server)
    {
        ASC.ClearAbility(C.DashAbilityHandle);
        FGameplayAbilitySpec Spec(URPGGameplayAbility_RidingDash::StaticClass(),1,INDEX_NONE,&C); Spec.Handle=Server.DashGrant.AbilityHandle; ASC.GiveAbility(Spec);
        C.State=Server.State; C.DashGrant=Server.DashGrant;
    }
    static double Travelled(const URPGRidingComponent& C) { return C.DashPrediction->GetTravelled(); }
};
namespace
{
    constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        ARPGRidingDashPredictionTestController* PC;
        ARPGRidingDashPredictionTestPawn* Pawn;
        URPGRidingDashPredictionTestASC* ASC;
        URPGRidingDashTestMovement* Move;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        uint64 Frame=0;
        FFixture(float GroundX=3000)
        {
            const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            World->SetGameState(World->SpawnActor<AGameStateBase>());
            Box(FVector(0,0,-50),FVector(GroundX,2000,50));
            PC=World->SpawnActor<ARPGRidingDashPredictionTestController>(); PC->SetAsLocalPlayerController();
            auto* PS=World->SpawnActor<APlayerState>();PC->SetPlayerState(PS);PS->SetOwner(PC);
            Session=NewObject<URPGRidingSessionComponent>(PS);Session->RegisterComponent();
            FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn=World->SpawnActor<ARPGRidingDashPredictionTestPawn>(FVector(0,0,200),FRotator::ZeroRotator,P);PC->Possess(Pawn);
            ASC=CastChecked<URPGRidingDashPredictionTestASC>(Pawn->ASC);ASC->InitAbilityActorInfo(Pawn,Pawn);ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100);Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Move=CastChecked<URPGRidingDashTestMovement>(Pawn->GetCharacterMovement());Riding=Pawn->FindComponentByClass<URPGRidingComponent>();
            RPGPrepareRidingMovementTestSession(*Session,*Riding);RPGUnprotectRidingDashTestSession(*Session);
            Move->SetMovementMode(MOVE_Falling);Tick(40);Riding->RequestSetMounted(true,{});
        }
        void Box(FVector P,FVector E)
        {
            auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);
            B->SetRelativeLocation(P);B->SetMobility(EComponentMobility::Static);B->SetBoxExtent(E);B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            B->SetCollisionObjectType(ECC_WorldStatic);B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();
        }
        void Tick(int32 Count=1)
        {
            for(int32 I=0;I<Count;++I)
            {
                Frame=FMath::Max(Frame,GFrameCounter)+1;TGuardValue<uint64> Guard(GFrameCounter,Frame);World->Tick(LEVELTICK_All,1.f/60);
                // There is no NetDriver/Player/possession handshake in this fixture.
                // Deliver the native movement step explicitly instead of pretending a move RPC arrived.
                if((Pawn->GetLocalRole()==ROLE_AutonomousProxy&&PC->bTestLocal)||(Pawn->HasAuthority()&&!PC->bTestLocal))
                    Move->StepTestAutonomous(World->GetTimeSeconds(),1.f/60);
            }
        }
        void Client(){Pawn->SetRole(ROLE_AutonomousProxy);PC->SetRole(ROLE_AutonomousProxy);}
        FGameplayAbilitySpecHandle Handle()const{FRPGRidingDashGrant G;Riding->TryGetDashGrant(G);return G.AbilityHandle;}
        void Reject(){ASC->ClientActivateAbilityFailed_Implementation(Handle(),ASC->SentKey.Current);}
        ~FFixture(){PC->SetRole(ROLE_Authority);Pawn->SetRole(ROLE_Authority);PC->bTestLocal=true;RPGTestWorld::Destroy(World);}
    };
    FRPGRidingRepState Ended(FRPGRidingRepState S)
    {
        ++S.Key.ActionSequence;S.Action=ERPGRidingAction::None;S.ExecutionId.Invalidate();S.ActionDuration=0;S.ActionStartServerWorldSeconds=0;S.ActionDirection=FVector::ZeroVector;
        S.LastEndReason=ERPGRidingEndReason::Completed;return S;
    }
    FGameplayAbilityTargetDataHandle CopyWire(const FGameplayAbilityTargetDataHandle& Data)
    {
        const auto* V=FRPGRidingDashIntent::Read(Data);FGameplayAbilityTargetDataHandle Out;
        if(!V)return Out;
        auto Saved=*V;TArray<uint8> Bytes;FMemoryWriter W(Bytes);bool Ok=false;Saved.NetSerialize(W,nullptr,Ok);
        auto* Received=new FRPGRidingDashIntent();FMemoryReader R(Bytes);Received->NetSerialize(R,nullptr,Ok);Out.Add(Received);return Out;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionWire,"ProjectRPG.Riding.DashPrediction.Wire",Flags)
bool FRidingDashPredictionWire::RunTest(const FString&)
{
    FRPGRidingDashIntent I;I.GrantId=FGuid::NewGuid();I.Expected={FGuid::NewGuid(),2,4};I.Direction=FVector::ForwardVector;
    TArray<uint8> Bytes;FMemoryWriter W(Bytes);bool Ok=false;TestTrue(TEXT("Input wire is bounded"),I.NetSerialize(W,nullptr,Ok)&&Ok&&Bytes.Num()<=80);
    FRPGRidingDashIntent Copy;FMemoryReader R(Bytes);TestTrue(TEXT("Input round trip"),Copy.NetSerialize(R,nullptr,Ok)&&Copy.GrantId==I.GrantId&&RPGRiding::Compare(Copy.Expected,I.Expected)==ERPGRidingStateOrder::Same&&Copy.Direction==I.Direction);
    for(int32 N=0;N<6;++N)
    {
        auto Bad=I;if(N==0)Bad.GrantId.Invalidate();if(N==1)Bad.Expected.StateRevision=0;if(N==2)Bad.Expected.ActionSequence=MAX_uint32-1;
        if(N==3)Bad.Direction=FVector::ZeroVector;if(N==4)Bad.Direction=FVector::UpVector;if(N==5)Bad.Direction.X=std::numeric_limits<double>::quiet_NaN();
        TArray<uint8> B;FMemoryWriter BW(B);TestFalse(TEXT("Malformed input fails closed"),Bad.NetSerialize(BW,nullptr,Ok));
    }
    FGameplayAbilityTargetDataHandle D;TestNull(TEXT("Empty data rejected"),FRPGRidingDashIntent::Read(D));D.Add(new FRPGRidingDashIntent(I));
    TestNotNull(TEXT("Exactly one native input"),FRPGRidingDashIntent::Read(D));D.Add(new FGameplayAbilityTargetData_ActorArray());TestNull(TEXT("Mixed or multiple entries rejected"),FRPGRidingDashIntent::Read(D));
    Bytes.SetNum(3);FMemoryReader Short(Bytes);TestFalse(TEXT("Truncated input rejected"),Copy.NetSerialize(Short,nullptr,Ok));TestFalse(TEXT("Partial input cannot be used"),Copy.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionReflectedWire,"ProjectRPG.Riding.DashPrediction.ReflectedWire",Flags)
bool FRidingDashPredictionReflectedWire::RunTest(const FString&)
{
    // A native NetSerialize round trip cannot detect fields missing from Iris target data.
    FRPGRidingDashIntent Input; Input.GrantId=FGuid::NewGuid(); Input.Expected={FGuid::NewGuid(),7,13};
    Input.Direction=FVector(3,4,0).GetSafeNormal();
    TArray<uint8> Bytes; FMemoryWriter Writer(Bytes); FRPGRidingDashIntent::StaticStruct()->SerializeBin(Writer,&Input);
    FRPGRidingDashIntent Received; FMemoryReader Reader(Bytes); FRPGRidingDashIntent::StaticStruct()->SerializeBin(Reader,&Received);
    TestTrue(TEXT("Reflected payload preserves grant, exact state key and normalized direction"),
        !Writer.IsError() && !Reader.IsError() && Received.IsValid() && Received.GrantId==Input.GrantId
        && RPGRiding::Compare(Received.Expected,Input.Expected)==ERPGRidingStateOrder::Same && Received.Direction==Input.Direction);
#if UE_WITH_IRIS
    UE::Net::FReplicationStateDescriptorBuilder::FParameters Parameters;
    Parameters.SkipCheckForCustomNetSerializerForStruct=true;
    const auto Descriptor=UE::Net::FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(FRPGRidingDashIntent::StaticStruct(),Parameters);
    if(TestTrue(TEXT("Iris creates the full bounded input descriptor"),Descriptor.IsValid() && Descriptor->MemberCount==3))
    {
        for(const TCHAR* Name:{TEXT("GrantId"),TEXT("Expected"),TEXT("Direction")})
        {
            bool Found=false;
            for(uint16 Index=0;Index<Descriptor->MemberCount;++Index)
                Found |= Descriptor->MemberProperties[Index] && Descriptor->MemberProperties[Index]->GetFName()==FName(Name);
            TestTrue(FString::Printf(TEXT("Iris includes %s"),Name),Found);
        }
    }
#endif
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionLocalMovement,"ProjectRPG.Riding.DashPrediction.LocalMovement",Flags)
bool FRidingDashPredictionLocalMovement::RunTest(const FString&)
{
    FFixture F;F.Client();const auto Before=F.Riding->GetRidingState();const auto Timing=F.Session->GetOwnerState();
    if(!TestTrue(TEXT("Real owning-client GAS activation"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;
    TestTrue(TEXT("LocalPredicted execution policy"),GetDefault<URPGGameplayAbility_RidingDash>()->GetNetExecutionPolicy()==EGameplayAbilityNetExecutionPolicy::LocalPredicted);
    TestTrue(TEXT("Actual local GAS key and bounded intent sent once"),F.ASC->SentKey.IsLocalClientKey()&&F.ASC->ActivationRequests==1&&F.ASC->IntentRequests==1&&FRPGRidingDashIntent::Read(F.ASC->SentData));
    TestTrue(TEXT("Prediction does not publish authority or a security window"),F.Riding->GetRidingState().Action==ERPGRidingAction::None&&RPGRiding::Compare(Before.Key,F.Riding->GetRidingState().Key)==ERPGRidingStateOrder::Same&&!F.Riding->IsServerDashActive()&&F.Move->Opened==0);
    const auto S=F.Riding->GetDashMovementState();TestTrue(TEXT("Separate execution projection is canonical"),RPGRiding::IsCanonical(S)&&S.Action==ERPGRidingAction::Dash);
    TestTrue(TEXT("Temporary UI cooldown only"),F.Riding->GetPredictedDashCooldownRemaining()>9&&F.Session->GetOwnerState().DashEndServerSeconds==Timing.DashEndServerSeconds);
    F.Tick(10);TestTrue(TEXT("Actual Engine CMC moves immediately on the owner"),F.Pawn->GetActorLocation().X>150&&F.Pawn->GetActorLocation().X<500);
    TestTrue(TEXT("Dash prevents sideways voluntary input"),F.Move->ConstrainInputAcceleration(FVector::RightVector).IsNearlyZero());
    F.Reject();TestFalse(TEXT("GAS rejection removes local movement"),F.Riding->IsDashMovementActive());TestEqual(TEXT("Rejected temporary cooldown cleared"),F.Riding->GetPredictedDashCooldownRemaining(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionEmote,"ProjectRPG.Riding.DashPrediction.EmotePreemption",Flags)
bool FRidingDashPredictionEmote::RunTest(const FString&)
{
    FFixture F;auto Emote=F.Riding->GetRidingState();++Emote.Key.ActionSequence;
    Emote.Action=ERPGRidingAction::Emote;Emote.ExecutionId=FGuid::NewGuid();Emote.ActionDuration=3;
    Emote.ActionStartServerWorldSeconds=F.World->GetTimeSeconds();
    F.Client();FRPGRidingDashPredictionTestAccess::State(*F.Riding,Emote);
    if(!TestTrue(TEXT("Dash predicts while authoritative Emote remains"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;
    F.Tick(2);const auto View=F.Riding->GetDashMovementState();
    TestTrue(TEXT("Prediction keeps authority and advances one action"),F.Riding->GetRidingState().Action==ERPGRidingAction::Emote&&View.Action==ERPGRidingAction::Dash&&View.Key.ActionSequence==Emote.Key.ActionSequence+1);
    F.Reject();TestTrue(TEXT("Rejected Dash restores original Emote projection"),!F.Riding->IsDashMovementActive()&&F.Riding->GetDashMovementState().ExecutionId==Emote.ExecutionId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionServerIntent,"ProjectRPG.Riding.DashPrediction.ServerIntent",Flags)
bool FRidingDashPredictionServerIntent::RunTest(const FString&)
{
    FFixture S,C;FRPGRidingDashPredictionTestAccess::Replica(*C.Riding,*C.ASC,*S.Riding);C.Client();S.PC->bTestLocal=false;
    if(!TestTrue(TEXT("Client prediction"),C.Riding->RequestDash()&&C.Riding->IsDashMovementActive()&&FRPGRidingDashIntent::Read(C.ASC->SentData)))return false;
    const auto H=S.Handle();S.ASC->ActivateRemote(H,C.ASC->SentKey);
    TestTrue(TEXT("Server waits without movement or cooldown"),S.Riding->GetRidingState().Action==ERPGRidingAction::None&&S.Move->Opened==0&&S.Session->GetOwnerState().DashEndServerSeconds<=S.World->GetTimeSeconds());
    const auto Data=CopyWire(C.ASC->SentData);S.ASC->ServerSetReplicatedTargetData_Implementation(H,C.ASC->SentKey,Data,{},C.ASC->SentMessageKey);
    if(!TestTrue(TEXT("Actual GAS data delegate admits server motion"),S.Riding->IsServerDashActive()&&S.Move->Opened==1))return false;
    const auto Authority=S.Riding->GetRidingState();const auto Local=C.Riding->GetDashMovementState();
    TestTrue(TEXT("Both executions share the real GAS key identity"),Authority.ExecutionId==Local.ExecutionId&&RPGRiding::Compare(Authority.Key,Local.Key)==ERPGRidingStateOrder::Same);
    C.ASC->ClientActivateAbilitySucceed_Implementation(C.Handle(),C.ASC->SentKey);FRPGRidingDashPredictionTestAccess::State(*C.Riding,Authority);
    S.Tick();C.Tick();
    auto Group=S.Move->CurrentRootMotion;C.Move->RemapTestServerSources(Group,1.f);
    const auto LocalSource=C.Move->CurrentRootMotion.RootMotionSources.IsEmpty()?nullptr:C.Move->CurrentRootMotion.RootMotionSources[0];
    TestTrue(TEXT("Engine RMS mapper correlates different server/client local IDs"),LocalSource&&Group.RootMotionSources.Num()==1&&Group.RootMotionSources[0]->LocalID==LocalSource->LocalID);
    TestTrue(TEXT("Owner accepts authority without another RMS"),C.Move->CurrentRootMotion.RootMotionSources.Num()==1&&C.Riding->IsDashMovementActive());
    S.ASC->CancelAbilityHandle(H);FRPGRidingDashPredictionTestAccess::State(*C.Riding,S.Riding->GetRidingState());
    TestFalse(TEXT("Authority end retires predicted source"),C.Riding->IsDashMovementActive());TestTrue(TEXT("Accepted server cooldown survives cancellation"),S.Session->GetOwnerState().DashEndServerSeconds>S.World->GetTimeSeconds()+8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionRejection,"ProjectRPG.Riding.DashPrediction.Rejection",Flags)
bool FRidingDashPredictionRejection::RunTest(const FString&)
{
    FFixture F;F.Client();if(!TestTrue(TEXT("Client prediction started"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;const auto H=F.Handle();
    F.ASC->ClientActivateAbilityFailed_Implementation(H,F.ASC->SentKey.Current==MAX_int16?1:F.ASC->SentKey.Current+1);
    TestTrue(TEXT("Another activation rejection cannot stop this prediction"),F.Riding->IsDashMovementActive());
    const auto Name=F.Move->CurrentRootMotion.PendingAddRootMotionSources[0]->InstanceName;
    auto Foreign=MakeShared<FRootMotionSource_ConstantForce>();Foreign->InstanceName=Name;Foreign->Force=FVector(0,40,0);Foreign->Duration=5;
    const auto ForeignId=F.Move->ApplyRootMotionSource(Foreign);F.Pawn->SavedRootMotion=F.Move->CurrentRootMotion;
    FSavedMovePtr Saved(new FSavedMove_RPGRiding());Saved->SavedRootMotion=F.Move->CurrentRootMotion;auto* Prediction=F.Move->GetPredictionData_Client_Character();Prediction->SavedMoves.Add(Saved);Prediction->PendingMove=Saved;
    F.Reject();const auto Remaining=F.Move->GetRootMotionSourceByID(ForeignId);
    TestTrue(TEXT("Same-name foreign RMS survives exact predicted cleanup"),Remaining&&Remaining->GetScriptStruct()==FRootMotionSource_ConstantForce::StaticStruct());
    const auto HasDash=[](const FRootMotionSourceGroup& G){const auto Dash=[](const auto& Root){return Root&&Root->GetScriptStruct()==FRootMotionSource_RPGRidingDash::StaticStruct();};return G.RootMotionSources.ContainsByPredicate(Dash)||G.PendingAddRootMotionSources.ContainsByPredicate(Dash);};
    TestFalse(TEXT("Current/character/saved/pending contain no retired Dash"),HasDash(F.Move->CurrentRootMotion)||HasDash(F.Pawn->SavedRootMotion)||HasDash(Saved->SavedRootMotion));
    TestEqual(TEXT("Rejected UI cooldown cleared"),F.Riding->GetPredictedDashCooldownRemaining(),0.f);
    {
        FFixture G;G.Client();if(!TestTrue(TEXT("Another actual client activation"),G.Riding->RequestDash()&&G.Riding->IsDashMovementActive()))return false;
        const auto Handle=G.Handle();G.ASC->ClientActivateAbilitySucceed_Implementation(Handle,G.ASC->SentKey);
        const auto* Ability=G.ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance();
        G.ASC->ClientCancelAbility_Implementation(Handle,Ability->GetCurrentActivationInfo());
        TestFalse(TEXT("Server cancellation after GAS confirmation retires owner RMS"),G.Riding->IsDashMovementActive());
        TestEqual(TEXT("Intent rejection after GAS confirmation clears temporary cooldown"),G.Riding->GetPredictedDashCooldownRemaining(),0.f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionReconcile,"ProjectRPG.Riding.DashPrediction.Reconcile",Flags)
bool FRidingDashPredictionReconcile::RunTest(const FString&)
{
    FFixture F;F.Client();if(!F.Riding->RequestDash())return false;const auto Predicted=F.Riding->GetDashMovementState();
    const auto Old=F.Move->CurrentRootMotion;F.Reject();FRPGRidingDashPredictionTestAccess::State(*F.Riding,Predicted);
    F.Move->CurrentRootMotion=Old;RPGRidingDashMotion::Reconcile(*F.Pawn,F.Riding->GetDashMovementState(),true);
    TestFalse(TEXT("Late authority source cannot revive a retired action"),F.Move->CurrentRootMotion.HasActiveRootMotionSources());
    TestTrue(TEXT("Retirement does not rewrite authoritative State"),F.Riding->GetRidingState().ExecutionId==Predicted.ExecutionId);
    FRPGRidingDashPredictionTestAccess::State(*F.Riding,Ended(Predicted));
    if(!TestTrue(TEXT("Fresh action can predict after newer authority state"),F.Riding->RequestDash()))return false;
    const auto New=F.Riding->GetDashMovementState();F.Move->CurrentRootMotion.PendingAddRootMotionSources.Append(Old.PendingAddRootMotionSources);
    FSavedMove_RPGRiding Stale;Stale.Riding=FRPGRidingMoveStamp::FromState(Predicted);Stale.SavedRootMotion=Old;Stale.PrepMoveFor(F.Pawn);
    TestTrue(TEXT("Old saved source discarded while fresh prediction remains"),F.Riding->IsDashMovementActive()&&New.ExecutionId!=Predicted.ExecutionId
        &&F.Move->CurrentRootMotion.RootMotionSources.Num()+F.Move->CurrentRootMotion.PendingAddRootMotionSources.Num()==1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionReplay,"ProjectRPG.Riding.DashPrediction.Replay",Flags)
bool FRidingDashPredictionReplay::RunTest(const FString&)
{
    FFixture F;F.Client();if(!F.Riding->RequestDash())return false;F.Tick(4);
    FSavedMovePtr SavedPtr(new FSavedMove_RPGRiding());auto& Saved=static_cast<FSavedMove_RPGRiding&>(*SavedPtr);auto* Data=F.Move->GetPredictionData_Client_Character();
    Data->SavedMoves.Add(SavedPtr);Saved.SetMoveFor(F.Pawn,1.f/60,FVector::ZeroVector,*Data);Saved.SavedRootMotion=F.Move->CurrentRootMotion;
    const auto Position=F.Pawn->GetActorLocation();const double Distance=FRPGRidingDashPredictionTestAccess::Travelled(*F.Riding);F.Tick(4);
    TestTrue(TEXT("Prediction records actual travelled path"),FRPGRidingDashPredictionTestAccess::Travelled(*F.Riding)>Distance+20);
    F.Pawn->SetActorLocation(Position,false);Saved.PrepMoveFor(F.Pawn);
    TestTrue(TEXT("Replay restores only this live prediction's distance budget"),FMath::IsNearlyEqual(FRPGRidingDashPredictionTestAccess::Travelled(*F.Riding),Distance,.001));
    FSavedMovePtr Next(new FSavedMove_RPGRiding());static_cast<FSavedMove_RPGRiding&>(*Next).Riding=Saved.Riding;TestFalse(TEXT("Executing moves cannot combine"),Saved.CanCombineWith(Next,F.Pawn,1));
    F.Reject();Saved.PrepMoveFor(F.Pawn);TestFalse(TEXT("Saved move cannot regrant a rejected prediction"),F.Riding->IsDashMovementActive()||Saved.SavedRootMotion.HasActiveRootMotionSources());
    Saved.Clear();TestEqual(TEXT("Pooled move clears local progress"),Saved.PredictedDashTravelled,0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionBoundaries,"ProjectRPG.Riding.DashPrediction.Boundaries",Flags)
bool FRidingDashPredictionBoundaries::RunTest(const FString&)
{
    { FFixture F;F.Box(FVector(300,0,200),FVector(25,1000,300));F.Client();if(!TestTrue(TEXT("Wall prediction started"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;F.Tick(60);
      TestTrue(TEXT("Owner capsule stops at wall"),F.Pawn->GetActorLocation().X>150&&F.Pawn->GetActorLocation().X<250&&!F.Riding->IsDashMovementActive());TestEqual(TEXT("Owner never opens server window"),F.Move->Opened,0); }
    { FFixture F(500);F.Client();if(!TestTrue(TEXT("Edge prediction started"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;F.Tick(60);
      TestTrue(TEXT("Owner clips at walkable edge"),F.Pawn->GetActorLocation().X>300&&F.Pawn->GetActorLocation().X<600&&!F.Riding->IsDashMovementActive()); }
    { FFixture F(850);F.Client();if(!TestTrue(TEXT("Final-frame edge prediction started"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;F.Tick(36);
      TestTrue(TEXT("A source finishing this frame still clips its final force delta"),F.Pawn->GetActorLocation().X>800&&F.Pawn->GetActorLocation().X<895&&!F.Riding->IsDashMovementActive()); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashPredictionLifetime,"ProjectRPG.Riding.DashPrediction.Lifetime",Flags)
bool FRidingDashPredictionLifetime::RunTest(const FString&)
{
    { FFixture S,C;FRPGRidingDashPredictionTestAccess::Replica(*C.Riding,*C.ASC,*S.Riding);C.Client();if(!TestTrue(TEXT("Timeout fixture predicts"),C.Riding->RequestDash()&&FRPGRidingDashIntent::Read(C.ASC->SentData)))return false;S.PC->bTestLocal=false;
      const auto H=S.Handle();S.ASC->ActivateRemote(H,C.ASC->SentKey);S.Tick(150);const auto* Spec=S.ASC->FindAbilitySpecFromHandle(H);
      TestTrue(TEXT("Missing intent expires without cooldown/window"),Spec&&!Spec->IsActive()&&S.Move->Opened==0&&S.Session->GetOwnerState().DashEndServerSeconds<=S.World->GetTimeSeconds()); }
    { FFixture S,C;FRPGRidingDashPredictionTestAccess::Replica(*C.Riding,*C.ASC,*S.Riding);C.Client();if(!TestTrue(TEXT("Grant fixture predicts"),C.Riding->RequestDash()&&FRPGRidingDashIntent::Read(C.ASC->SentData)))return false;S.PC->bTestLocal=false;
      const auto H=S.Handle();S.ASC->ActivateRemote(H,C.ASC->SentKey);auto Data=CopyWire(C.ASC->SentData);static_cast<FRPGRidingDashIntent*>(Data.Get(0))->GrantId=FGuid::NewGuid();
      S.ASC->ServerSetReplicatedTargetData_Implementation(H,C.ASC->SentKey,Data,{},C.ASC->SentMessageKey);const auto* Spec=S.ASC->FindAbilitySpecFromHandle(H);
      TestTrue(TEXT("Another grant cannot spend server cooldown/window"),Spec&&!Spec->IsActive()&&S.Move->Opened==0&&S.Session->GetOwnerState().DashEndServerSeconds<=S.World->GetTimeSeconds()); }
    { FFixture F;F.Client();F.Riding->RequestDash();F.PC->bTestLocal=false;F.Tick(12);TestFalse(TEXT("Losing local possession retires prediction"),F.Riding->IsDashMovementActive());TestEqual(TEXT("Ownership loss clears temporary cooldown"),F.Riding->GetPredictedDashCooldownRemaining(),0.f); }
    { FFixture F;F.Client();if(!TestTrue(TEXT("Live owner predicts"),F.Riding->RequestDash()&&F.Riding->IsDashMovementActive()))return false;
      F.Pawn->Attributes->CurrentHealth.SetBaseValue(0);F.Pawn->Attributes->CurrentHealth.SetCurrentValue(0);F.Tick(12);
      TestFalse(TEXT("Replicated death cancels before the Riding state update"),F.Riding->IsDashMovementActive());TestEqual(TEXT("Death clears predicted cooldown"),F.Riding->GetPredictedDashCooldownRemaining(),0.f); }
    return true;
}
#endif
