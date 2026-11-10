#include "Tests/RPGRidingDashMotionTests.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingMovementPrediction.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/RootMotionSource.h"
#include "Tests/RPGTestWorldCleanup.h"

ARPGRidingDashTestPawn::ARPGRidingDashTestPawn(const FObjectInitializer& I)
    : Super(I.SetDefaultSubobjectClass<URPGRidingDashTestMovement>(ACharacter::CharacterMovementComponentName))
{
    ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ASC"));
    Attributes = CreateDefaultSubobject<URPGRidingTestAttributes>(TEXT("Attributes"));
    CreateDefaultSubobject<URPGRidingComponent>(TEXT("Riding"));
}
FRPGRidingDashWindow URPGRidingDashTestMovement::OpenRidingDashWindow(float D, float L)
{
    Window = FGuid::NewGuid(); ++Opened; Duration = D; Distance = L;
    const FGuid Handle = Window;
    FRPGRidingDashWindow W{this, Handle, [Weak = TWeakObjectPtr<URPGRidingDashTestMovement>(this), Handle]()
        { if (auto* M = Weak.Get(); M && M->Window == Handle) { M->Window.Invalidate(); ++M->Closed; } }};
    auto Callback = MoveTemp(OnOpen); OnOpen = nullptr;
    if (Callback) Callback();
    return W;
}
void URPGRidingDashTestMovement::PerformMovement(float Delta)
{
    if (auto* R = CharacterOwner->FindComponentByClass<URPGRidingComponent>()) { R->ValidateServerMovementContext(); R->PrepareServerDashMovement(); R->PrepareDashPredictionMovement(); }
    Super::PerformMovement(Delta);
    if (auto* R = CharacterOwner->FindComponentByClass<URPGRidingComponent>()) { R->FinalizeServerDashMovement(); R->FinalizeDashPredictionMovement(); }
}
FNetworkPredictionData_Client* URPGRidingDashTestMovement::GetPredictionData_Client() const
{
    if (!ClientPredictionData) const_cast<URPGRidingDashTestMovement*>(this)->ClientPredictionData = new FPredictionData_Client_RPGRiding(*this);
    return ClientPredictionData;
}
float URPGRidingDashTestMovement::GetMaxSpeed() const
{
    auto* R = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    FRPGRidingMovementProfile P;
    return R && R->TryBuildMovementProfile(P) ? P.Speed : Super::GetMaxSpeed();
}
FVector URPGRidingDashTestMovement::ConstrainInputAcceleration(const FVector& Input) const
{
    auto* R = CharacterOwner ? CharacterOwner->FindComponentByClass<URPGRidingComponent>() : nullptr;
    return R && R->IsDashMovementActive() ? FVector::ZeroVector : Super::ConstrainInputAcceleration(Input);
}
bool URPGRidingDashTestMovement::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool Sweep, FHitResult* Hit, ETeleportType Teleport)
{
    auto* R = CharacterOwner->FindComponentByClass<URPGRidingComponent>();
    const FVector Start = UpdatedComponent->GetComponentLocation();
    const FGuid Execution = R ? R->GetDashMovementState().ExecutionId : FGuid();
    const double Fraction = R ? R->ConstrainServerMovement(Start, Start + Delta) * R->ConstrainPredictedDashMovement(Start, Start + Delta) : 1;
    const bool Moved = Super::MoveUpdatedComponentImpl(Delta * Fraction, Rotation, Sweep, Hit, Teleport);
    if (R) { R->RecordServerDashMovement(Execution, Start, UpdatedComponent->GetComponentLocation()); R->RecordPredictedDashMovement(Execution, Start, UpdatedComponent->GetComponentLocation()); }
    return Moved;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        ARPGRidingDashTestPawn* Pawn;
        URPGRidingDashTestMovement* Move;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        uint64 Frame = 0;
        FFixture(float GroundX = 3000)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            Box(FVector(0,0,-50), FVector(GroundX, 2000, 50));
            auto* PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0,0,200), FRotator::ZeroRotator, P); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Move = CastChecked<URPGRidingDashTestMovement>(Pawn->GetCharacterMovement()); Move->MaxWalkSpeed = 400;
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            RPGUnprotectRidingDashTestSession(*Session);
            Move->SetMovementMode(MOVE_Falling); for (int32 I=0; I<40; ++I) Tick();
            Riding->RequestSetMounted(true, {});
        }
        void Box(FVector Position, FVector Extent)
        {
            auto* A = World->SpawnActor<AActor>(); auto* B = NewObject<UBoxComponent>(A); A->SetRootComponent(B); A->AddInstanceComponent(B);
            B->SetRelativeLocation(Position); B->SetMobility(EComponentMobility::Static); B->SetBoxExtent(Extent); B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            B->SetCollisionObjectType(ECC_WorldStatic); B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent();
        }
        ERPGRidingResultCode Start() { return Riding->StartServerDash(Riding->GetRidingState().Key, FVector::ForwardVector); }
        void Tick(bool Input = false)
        {
            Frame = FMath::Max(Frame, GFrameCounter) + 1; TGuardValue<uint64> Guard(GFrameCounter, Frame);
            if (Input) Pawn->AddMovementInput(FVector::RightVector);
            World->Tick(LEVELTICK_All, 1.f/60);
        }
        void UntilEnded()
        { for (int32 I=0; I<80 && Riding->GetRidingState().Action == ERPGRidingAction::Dash; ++I) Tick(true); }
        void ForeignRoot()
        {
            auto S = MakeShared<FRootMotionSource_ConstantForce>(); S->InstanceName=TEXT("ForeignControl"); S->Duration=1;
            S->Priority=200; S->AccumulateMode=ERootMotionAccumulateMode::Override; S->Force=FVector(0,100,0); Move->ApplyRootMotionSource(S);
        }
        ~FFixture() { RPGTestWorld::Destroy(World); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashMotionTravelTest, "ProjectRPG.Riding.DashMotion.Travel", Flags)
bool FRidingDashMotionTravelTest::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Actual server execution admitted"), F.Start() == ERPGRidingResultCode::Applied && F.Riding->IsServerDashActive())) return false;
    TestEqual(TEXT("One owned security window"), F.Move->Opened, 1);
    TestTrue(TEXT("Finite admitted security budget"), F.Move->Distance == 925.f && F.Move->Duration <= .81f);
    F.UntilEnded();
    const auto P = F.Pawn->GetActorLocation();
    TestTrue(TEXT("Actual RMS travels forward, at most admitted 900cm"), P.X > 800 && P.X <= 900.1);
    TestTrue(TEXT("Voluntary side acceleration does not steer dash"), FMath::Abs(P.Y) < .1);
    TestFalse(TEXT("Completion closes own window"), F.Move->Window.IsValid());
    TestTrue(TEXT("Completion keeps canonical mounted state"), RPGRiding::IsCanonical(F.Riding->GetRidingState()) && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    TestTrue(TEXT("No residual dash speed"), F.Move->Velocity.Size2D() <= 600.1);
    TestTrue(TEXT("Cooldown not refunded"), F.Session->GetOwnerState().DashEndServerSeconds > F.World->GetTimeSeconds() + 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashMotionBoundaryTest, "ProjectRPG.Riding.DashMotion.Boundaries", Flags)
bool FRidingDashMotionBoundaryTest::RunTest(const FString&)
{
    {
        FFixture F; F.Move->SetGravityDirection(FVector(0,1,0));
        TestTrue(TEXT("World-XY dash refuses unsupported gravity before admission"), F.Start() == ERPGRidingResultCode::ActionBlocked);
        TestEqual(TEXT("Unsupported gravity allocates no window"), F.Move->Opened, 0);
        F.Move->SetGravityDirection(FVector(0,0,-1));
    }
    {
        FFixture F; F.Box(FVector(F.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+5,0,120), FVector(5,200,120));
        TestTrue(TEXT("Completely blocked start rejected before admission"), F.Start() == ERPGRidingResultCode::ActionBlocked);
        TestEqual(TEXT("Blocked start allocates no window"), F.Move->Opened, 0);
        TestTrue(TEXT("Blocked start does not consume cooldown"), F.Session->GetOwnerState().DashEndServerSeconds <= F.World->GetTimeSeconds()+.1);
    }
    {
        FFixture F; F.Box(FVector(300,0,120), FVector(10,200,120)); F.Start(); F.UntilEnded();
        TestTrue(TEXT("Capsule stops at first wall"), F.Pawn->GetActorLocation().X <= 300-10-F.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+.1);
        TestFalse(TEXT("Collision releases window"), F.Move->Window.IsValid());
    }
    {
        FFixture F(100); F.Start(); F.UntilEnded();
        TestTrue(TEXT("No voluntary crossing of unsupported ground"), F.Pawn->GetActorLocation().X < 145 && F.Move->MovementMode == MOVE_Walking);
        TestFalse(TEXT("Cliff termination releases window"), F.Move->Window.IsValid());
        const double StoppedX = F.Pawn->GetActorLocation().X;
        for (int32 I=0; I<8; ++I) F.Tick();
        TestTrue(TEXT("No leftover dash momentum walks off the cliff"), FMath::Abs(F.Pawn->GetActorLocation().X-StoppedX) < .1 && F.Move->MovementMode == MOVE_Walking);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashMotionOwnershipTest, "ProjectRPG.Riding.DashMotion.Ownership", Flags)
bool FRidingDashMotionOwnershipTest::RunTest(const FString&)
{
    FFixture F; F.Start(); F.Tick();
    const auto NewWindow = F.Move->OpenRidingDashWindow(1, 300); F.ForeignRoot(); F.Move->Velocity.Z=123;
    F.Riding->ForceDismount(ERPGRidingEndReason::Control);
    TestTrue(TEXT("Stale close cannot cancel newer window"), F.Move->Window == NewWindow.Handle);
    TestTrue(TEXT("Foreign RMS retained"), F.Move->GetRootMotionSource(TEXT("ForeignControl")).IsValid());
    TestEqual(TEXT("External vertical velocity preserved"), F.Move->Velocity.Z, 123.0);
    TestFalse(TEXT("Own dash no longer active"), F.Riding->IsServerDashActive());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashMotionPreemptionTest, "ProjectRPG.Riding.DashMotion.Preemption", Flags)
bool FRidingDashMotionPreemptionTest::RunTest(const FString&)
{
    FFixture F; F.Start(); F.Tick();
    F.Riding->StopServerDashForControl();
    TestTrue(TEXT("Preemption clears action before foreign window"), F.Riding->GetRidingState().Action == ERPGRidingAction::None);
    TestTrue(TEXT("Foreign control can acquire a free window"), F.Move->CanOpenRidingDashWindow());
    const auto Window = F.Move->OpenRidingDashWindow(1, 300); F.ForeignRoot();
    F.Riding->ForceDismount(ERPGRidingEndReason::Control);
    TestTrue(TEXT("Later Riding cleanup keeps control window"), F.Move->Window == Window.Handle);
    TestTrue(TEXT("Later Riding cleanup keeps control RMS"), F.Move->GetRootMotionSource(TEXT("ForeignControl")).IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashMotionCallbackTest, "ProjectRPG.Riding.DashMotion.Callbacks", Flags)
bool FRidingDashMotionCallbackTest::RunTest(const FString&)
{
    {
        FFixture F; F.Move->OnOpen = [&]() { F.Riding->ForceDismount(ERPGRidingEndReason::Manual); };
        TestTrue(TEXT("Admission may be cancelled by resource callback"), F.Start() == ERPGRidingResultCode::Applied);
        TestFalse(TEXT("Late acquired window is released"), F.Move->Window.IsValid());
        TestFalse(TEXT("Callback cannot resurrect RMS"), F.Riding->IsServerDashActive());
    }
    {
        FFixture F; FGuid NewHandle;
        F.Move->OnOpen = [&]() { F.Riding->ForceDismount(ERPGRidingEndReason::Manual); NewHandle=F.Move->OpenRidingDashWindow(1,50).Handle; F.ForeignRoot(); };
        F.Start();
        TestTrue(TEXT("Late original close keeps callback's new window"), F.Move->Window == NewHandle);
        TestTrue(TEXT("Callback's external RMS survives"), F.Move->GetRootMotionSource(TEXT("ForeignControl")).IsValid());
    }
    return true;
}
#endif
