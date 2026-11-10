#include "Riding/RPGRidingDashGrant.h"
#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Tests/RPGRidingDashMotionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Net/UnrealNetwork.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingDashGrantTestAccess
{
    static FRPGRidingDashGrant Raw(const URPGRidingComponent& C) { return C.DashGrant; }
    static void Install(URPGRidingComponent& C, const FRPGRidingDashGrant& G) { C.DashGrant = G; }
    static bool Current(const URPGRidingComponent& C, const FRPGRidingDashActivation& A) { return C.IsDashActivationCurrent(A); }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FRPGRidingDashGrant Grant()
    {
        FRPGRidingDashGrant G; G.GrantId = FGuid::NewGuid(); G.RiderEpoch = FGuid::NewGuid();
        G.MountRevision = 2; G.AbilityHandle = FGameplayAbilitySpec(URPGGameplayAbility_RidingDash::StaticClass()).Handle; G.CatalogVersion = 1;
        G.Distance = 900; G.Duration = .6f; G.Cooldown = 10; return G;
    }
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingDashTestPawn* Pawn;
        URPGRidingDashTestMovement* Move;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        uint64 Frame = 0;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            auto* Floor = World->SpawnActor<AActor>(); auto* B = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(B); Floor->AddInstanceComponent(B);
            B->SetRelativeLocation(FVector(0,0,-50)); B->SetMobility(EComponentMobility::Static); B->SetBoxExtent(FVector(3000,2000,50));
            B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); B->SetCollisionObjectType(ECC_WorldStatic); B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent();
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>(); PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0,0,200), FRotator::ZeroRotator, P); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Move = CastChecked<URPGRidingDashTestMovement>(Pawn->GetCharacterMovement());
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding); RPGUnprotectRidingDashTestSession(*Session);
            Move->SetMovementMode(MOVE_Falling); for (int32 I=0; I<40; ++I) Tick(); Riding->RequestSetMounted(true, {});
        }
        void Tick() { Frame = FMath::Max(Frame,GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter,Frame); World->Tick(LEVELTICK_All, 1.f/60); }
        ~FFixture() { Pawn->SetRole(ROLE_Authority); RPGTestWorld::Destroy(World); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantWire, "ProjectRPG.Riding.DashGrant.Wire", Flags)
bool FRidingDashGrantWire::RunTest(const FString&)
{
    auto G = Grant(); TArray<uint8> Bytes; FMemoryWriter W(Bytes); bool Ok = false;
    TestTrue(TEXT("Fixed-size approved grant serializes"), G.NetSerialize(W,nullptr,Ok) && Ok && Bytes.Num() <= 64);
    FRPGRidingDashGrant Copy; FMemoryReader R(Bytes); TestTrue(TEXT("Wire restores exact profile and capability scope"), Copy.NetSerialize(R,nullptr,Ok) && Ok && Copy.SameGrant(G));
    FRPGRidingDashGrant Empty; TArray<uint8> E; FMemoryWriter EW(E); Empty.NetSerialize(EW,nullptr,Ok);
    FMemoryReader ER(E); TestTrue(TEXT("Empty revocation clears a previously valid snapshot"), Copy.NetSerialize(ER,nullptr,Ok) && Copy.IsEmpty());
    for (int32 I=0; I<4; ++I)
    {
        auto Bad = G; if(I==0) Bad.Duration=11; if(I==1) Bad.Cooldown=0; if(I==2) Bad.RiderEpoch.Invalidate(); if(I==3) Bad.GrantId.Invalidate();
        TArray<uint8> B; FMemoryWriter BW(B); TestFalse(TEXT("Malformed grant cannot serialize"), Bad.NetSerialize(BW,nullptr,Ok)); TestTrue(TEXT("Failure propagates"), !Ok && BW.IsError());
    }
    Bytes.SetNum(3); FMemoryReader Short(Bytes);
    TestFalse(TEXT("Truncated packet rejected"), Copy.NetSerialize(Short,nullptr,Ok)); TestTrue(TEXT("Partial decode publishes no profile"), Copy.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantIdentity, "ProjectRPG.Riding.DashGrant.Identity", Flags)
bool FRidingDashGrantIdentity::RunTest(const FString&)
{
    auto G=Grant(); FPredictionKey Key; Key.Current=11; FRPGRidingDashActivation A{G,Key}; FRPGRidingStateKey Expected{G.RiderEpoch,G.MountRevision,0};
    const auto Id=A.ExecutionFor(Expected); TestTrue(TEXT("Identical approved context computes the same nonzero execution"), Id.IsValid() && A.ExecutionFor(Expected)==Id);
    auto Other=A; ++Other.PredictionKey.Current; TestTrue(TEXT("Another prediction key has another execution"), Other.ExecutionFor(Expected)!=Id);
    auto Next=Expected; Next.ActionSequence=2; TestTrue(TEXT("Another action in the same grant is distinct"), A.ExecutionFor(Next)!=Id);
    Other=A; Other.Grant.GrantId=FGuid::NewGuid(); TestTrue(TEXT("Regrant scope is distinct"), Other.ExecutionFor(Expected)!=Id);
    Next=Expected; ++Next.StateRevision; TestFalse(TEXT("Old grant cannot identify a replacement mount"), A.ExecutionFor(Next).IsValid());
    Next=Expected; Next.ActionSequence=MAX_uint32-1; TestFalse(TEXT("Sequence overflow cannot mint an execution"), A.ExecutionFor(Next).IsValid());
    Other=A; Other.PredictionKey=FPredictionKey(); TestFalse(TEXT("Missing key cannot predict execution"), Other.ExecutionFor(Expected).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantLifecycle, "ProjectRPG.Riding.DashGrant.Lifecycle", Flags)
bool FRidingDashGrantLifecycle::RunTest(const FString&)
{
    FFixture F; FRPGRidingDashGrant G;
    if(!TestTrue(TEXT("Mounted owner reads approved grant"),F.Riding->TryGetDashGrant(G))) return false;
    TestTrue(TEXT("Profile copied from admitted policy"),G.Distance==900 && G.Duration==.6f && G.Cooldown==10 && G.Matches(F.Riding->GetRidingState()));
    auto Edited=G; Edited.Distance=1; FRPGRidingDashGrant Again; F.Riding->TryGetDashGrant(Again); TestTrue(TEXT("Caller edits cannot mutate authority"),Again.SameGrant(G));
    for(int32 I=0;I<12;++I) F.Tick(); F.Riding->TryGetDashGrant(Again); TestTrue(TEXT("Reconciliation retains immutable grant"),Again.SameGrant(G));
    F.Riding->GetClass()->SetUpRuntimeReplicationData();
    TArray<FLifetimeProperty> Properties; F.Riding->GetLifetimeReplicatedProps(Properties); const auto* P=FindFProperty<FProperty>(F.Riding->GetClass(),TEXT("DashGrant"));
    TestTrue(TEXT("Grant property replicates only to owner"),P && Properties.ContainsByPredicate([P](const auto& L){return L.RepIndex==P->RepIndex && L.Condition==COND_OwnerOnly;}));
    F.Riding->RequestSetMounted(false,{}); TestFalse(TEXT("Foot has no usable profile"),F.Riding->TryGetDashGrant(Again)); TestTrue(TEXT("Revocation clears stored snapshot before callbacks"),FRPGRidingDashGrantTestAccess::Raw(*F.Riding).IsEmpty() && Again.IsEmpty());
    F.Riding->RequestSetMounted(true,{}); if(!TestTrue(TEXT("Remount has a grant"),F.Riding->TryGetDashGrant(Again)))return false;
    TestTrue(TEXT("Remount replaces nonce and exact ability handle"),Again.GrantId!=G.GrantId && Again.AbilityHandle!=G.AbilityHandle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantActivation, "ProjectRPG.Riding.DashGrant.Activation", Flags)
bool FRidingDashGrantActivation::RunTest(const FString&)
{
    FFixture F; FRPGRidingDashGrant G; if(!TestTrue(TEXT("Grant"),F.Riding->TryGetDashGrant(G)))return false;
    const auto Expected=F.Riding->GetRidingState().Key; FPredictionKey Fake; Fake.Current=12; FRPGRidingDashActivation Dormant{G,Fake};
    TestFalse(TEXT("Profile and arbitrary key alone do not constitute active GAS"),FRPGRidingDashGrantTestAccess::Current(*F.Riding,Dormant));
    TestTrue(TEXT("Unbound metadata cannot start movement"),F.Riding->StartServerDash(Expected,FVector::ForwardVector,Dormant)==ERPGRidingResultCode::StaleState);
    if(!TestTrue(TEXT("Real GAS starts Dash"),F.Riding->RequestDash() && F.Riding->IsServerDashActive()))return false;
    const auto* Spec=F.Pawn->ASC->FindAbilitySpecFromHandle(G.AbilityHandle); const auto* Ability=Spec ? Spec->GetPrimaryInstance() : nullptr;
    if(!TestNotNull(TEXT("Actual GAS instance"),Ability))return false;
    FRPGRidingDashActivation A{G,Ability->GetCurrentActivationInfo().GetActivationPredictionKey()};
    TestTrue(TEXT("Actual activation key bound to current grant"),A.IsValid() && FRPGRidingDashGrantTestAccess::Current(*F.Riding,A));
    TestTrue(TEXT("Authority-local activation uses an actual server-initiated key"),A.PredictionKey.IsServerInitiatedKey());
    FPredictionKey ClientKey; ClientKey.Current=A.PredictionKey.Current;
    TestTrue(TEXT("Equal numeric client and server keys have distinct identities"),FRPGRidingDashActivation{G,ClientKey}.ExecutionFor(Expected)!=A.ExecutionFor(Expected));
    const auto S=F.Riding->GetRidingState(); TestEqual(TEXT("Authority computes the agreed execution ID"),S.ExecutionId,A.ExecutionFor(Expected));
    const FName Name(*FString::Printf(TEXT("RidingDash_%s"),*S.ExecutionId.ToString(EGuidFormats::Digits))); const auto Root=F.Move->GetRootMotionSource(Name);
    TestTrue(TEXT("Actual typed RMS carries the correlated execution"),Root.IsValid() && Root->GetScriptStruct()==FRootMotionSource_RPGRidingDash::StaticStruct() && static_cast<const FRootMotionSource_RPGRidingDash&>(*Root).IsCurrent(S));
    auto Wrong=A; Wrong.PredictionKey.Current=Wrong.PredictionKey.Current==MAX_int16 ? 1 : Wrong.PredictionKey.Current+1;
    TestFalse(TEXT("Another key cannot borrow active capability"),FRPGRidingDashGrantTestAccess::Current(*F.Riding,Wrong));
    F.Pawn->ASC->CancelAbilityHandle(G.AbilityHandle); TestFalse(TEXT("Ended GAS key no longer current"),FRPGRidingDashGrantTestAccess::Current(*F.Riding,A));
    FRPGRidingDashGrant After; TestTrue(TEXT("Action end retains profile"),F.Riding->TryGetDashGrant(After) && After.SameGrant(G));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantOwnerOrdering, "ProjectRPG.Riding.DashGrant.OwnerOrdering", Flags)
bool FRidingDashGrantOwnerOrdering::RunTest(const FString&)
{
    FFixture F; FRPGRidingDashGrant G,Out; if(!TestTrue(TEXT("Authority grant"),F.Riding->TryGetDashGrant(G)))return false;
    F.Pawn->SetRole(ROLE_AutonomousProxy); TestTrue(TEXT("Owner can resolve replicated profile/spec/state in any arrival order"),F.Riding->TryGetDashGrant(Out) && Out.SameGrant(G));
    for(int32 I=0;I<12;++I)F.Tick();
    TestTrue(TEXT("Client reconciliation retains the replicated approval"),F.Riding->TryGetDashGrant(Out) && Out.SameGrant(G));
    for(int32 I=0;I<5;++I)
    {
        auto Bad=G; if(I==0)Bad={}; if(I==1)Bad.RiderEpoch=FGuid::NewGuid(); if(I==2)++Bad.MountRevision; if(I==3)++Bad.CatalogVersion; if(I==4)Bad.AbilityHandle=FGameplayAbilitySpec(URPGGameplayAbility_RidingDash::StaticClass()).Handle;
        FRPGRidingDashGrantTestAccess::Install(*F.Riding,Bad); TestFalse(TEXT("Missing or mismatched replica pieces fail closed"),F.Riding->TryGetDashGrant(Out)); TestTrue(TEXT("No stale profile returned"),Out.IsEmpty());
    }
    FRPGRidingDashGrantTestAccess::Install(*F.Riding,G); F.Pawn->SetRole(ROLE_SimulatedProxy); TestFalse(TEXT("Observer cannot use retained owner snapshot"),F.Riding->TryGetDashGrant(Out));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGrantCallback, "ProjectRPG.Riding.DashGrant.Callback", Flags)
bool FRidingDashGrantCallback::RunTest(const FString&)
{
    FFixture F; FRPGRidingDashGrant G; if(!TestTrue(TEXT("Ready grant"),F.Riding->TryGetDashGrant(G)))return false;
    F.Move->OnOpen=[&]{auto Changed=G;Changed.GrantId=FGuid::NewGuid();FRPGRidingDashGrantTestAccess::Install(*F.Riding,Changed);};
    F.Riding->RequestDash(); const auto* Spec=F.Pawn->ASC->FindAbilitySpecFromHandle(G.AbilityHandle);
    TestFalse(TEXT("Changed grant callback retires original GAS/motion/window"),(Spec && Spec->IsActive()) || F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
    TestTrue(TEXT("Accepted action keeps cooldown after callback rejection"),F.Session->GetOwnerState().DashEndServerSeconds>F.World->GetTimeSeconds()+8);
    return true;
}
#endif
