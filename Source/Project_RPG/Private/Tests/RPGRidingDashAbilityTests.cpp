#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Tests/RPGRidingDashMotionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingDashAbilityTestAccess
{ static void Release(URPGRidingComponent& C) { C.ReleaseBinding(); } };
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingDashTestPawn* Pawn;
        URPGRidingDashTestMovement* Move;
        URPGRidingComponent* Riding;
        URPGRidingSessionComponent* Session;
        uint64 Frame = 0;
        explicit FFixture(bool Mounted = true)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            Box(FVector(0,0,-50), FVector(3000,2000,50));
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0,0,200), FRotator::ZeroRotator, P); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Move = CastChecked<URPGRidingDashTestMovement>(Pawn->GetCharacterMovement());
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            RPGUnprotectRidingDashTestSession(*Session);
            Move->SetMovementMode(MOVE_Falling); for (int32 I=0; I<40; ++I) Tick();
            if (Mounted) Riding->RequestSetMounted(true, {});
        }
        void Box(FVector Position, FVector Extent)
        {
            auto* A = World->SpawnActor<AActor>(); auto* B = NewObject<UBoxComponent>(A); A->SetRootComponent(B); A->AddInstanceComponent(B);
            B->SetRelativeLocation(Position); B->SetMobility(EComponentMobility::Static); B->SetBoxExtent(Extent); B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            B->SetCollisionObjectType(ECC_WorldStatic); B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent();
        }
        void Tick()
        { Frame = FMath::Max(Frame, GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter, Frame); World->Tick(LEVELTICK_All, 1.f/60); }
        FGameplayAbilitySpecHandle Handle() const
        {
            for (const auto& S : Pawn->ASC->GetActivatableAbilities())
                if (S.SourceObject.Get() == Riding && S.Ability->GetClass() == URPGGameplayAbility_RidingDash::StaticClass()) return S.Handle;
            return {};
        }
        bool Active(FGameplayAbilitySpecHandle H) const
        { const auto* S = Pawn->ASC->FindAbilitySpecFromHandle(H); return S && S->IsActive(); }
        ~FFixture() { RPGTestWorld::Destroy(World); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASGrantTest, "ProjectRPG.Riding.DashGAS.Grant", Flags)
bool FRidingDashGASGrantTest::RunTest(const FString&)
{
    FFixture F(false);
    TestFalse(TEXT("No foot grant"), F.Handle().IsValid());
    F.Riding->RequestSetMounted(true, {}); const auto First = F.Handle();
    if (!TestTrue(TEXT("Mounted grants exact source spec"), First.IsValid())) return false;
    for (int32 I=0; I<12; ++I) F.Tick();
    TestEqual(TEXT("Repeated reconciliation retains one handle"), F.Handle(), First);
    const auto Peer = F.Pawn->ASC->GiveAbility(FGameplayAbilitySpec(URPGGameplayAbility_RidingDash::StaticClass(), 1, INDEX_NONE, F.Pawn));
    TestFalse(TEXT("Another source cannot borrow Riding privilege"), F.Pawn->ASC->TryActivateAbility(Peer));
    const auto Forged = F.Pawn->ASC->GiveAbility(FGameplayAbilitySpec(URPGGameplayAbility_RidingDash::StaticClass(), 1, INDEX_NONE, F.Riding));
    TestFalse(TEXT("Matching source alone cannot forge the tracked capability"), F.Pawn->ASC->TryActivateAbility(Forged));
    TestFalse(TEXT("Ambiguous source grants cannot be selected by input"), F.Riding->CanRequestDash());
    F.Pawn->ASC->ClearAbility(Forged);
    TestTrue(TEXT("Removing ambiguous spec restores exact capability"), F.Riding->CanRequestDash());
    F.Riding->RequestSetMounted(false, {});
    TestNull(TEXT("Dismount clears only its exact spec"), F.Pawn->ASC->FindAbilitySpecFromHandle(First));
    TestNotNull(TEXT("Same-class foreign spec preserved"), F.Pawn->ASC->FindAbilitySpecFromHandle(Peer));
    F.Riding->RequestSetMounted(true, {});
    TestTrue(TEXT("Remount uses a new capability handle"), F.Handle().IsValid() && F.Handle() != First);
    AddExpectedMessage(TEXT("TryActivateAbility called with invalid Handle"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    TestFalse(TEXT("Removed handle cannot activate a new mount"), F.Pawn->ASC->TryActivateAbility(First));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASActivationTest, "ProjectRPG.Riding.DashGAS.Activation", Flags)
bool FRidingDashGASActivationTest::RunTest(const FString&)
{
    FFixture F; const auto H = F.Handle();
    TestTrue(TEXT("Ready owner can request Dash"), F.Riding->CanRequestDash());
    if (!TestTrue(TEXT("GAS accepts native request"), F.Riding->RequestDash())) return false;
    TestTrue(TEXT("GAS and actual motion live together"), F.Active(H) && F.Riding->IsServerDashActive());
    TestFalse(TEXT("Active execution rejects another input"), F.Riding->RequestDash());
    for (int32 I=0; I<80 && F.Riding->IsServerDashActive(); ++I) F.Tick();
    TestTrue(TEXT("Real server RMS traveled admitted distance"), F.Pawn->GetActorLocation().X > 800 && F.Pawn->GetActorLocation().X <= 900.1);
    TestFalse(TEXT("Motion completion ends GAS"), F.Active(H));
    TestEqual(TEXT("Completion keeps mounted capability"), F.Handle(), H);
    TestFalse(TEXT("Cooldown rejects owner input and direct GAS"), F.Riding->CanRequestDash() || F.Pawn->ASC->TryActivateAbility(H));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASCancelTest, "ProjectRPG.Riding.DashGAS.Cancel", Flags)
bool FRidingDashGASCancelTest::RunTest(const FString&)
{
    FFixture F; const auto H = F.Handle();
    if (!TestTrue(TEXT("Dash started before cancellation"), F.Riding->RequestDash() && F.Riding->IsServerDashActive() && F.Active(H))) return false;
    F.Tick();
    const double Cooldown = F.Session->GetOwnerState().DashEndServerSeconds;
    F.Pawn->ASC->CancelAbilityHandle(H);
    TestFalse(TEXT("Cancel ends GAS and motion"), F.Active(H) || F.Riding->IsServerDashActive());
    TestFalse(TEXT("Cancel releases original window"), F.Move->Window.IsValid());
    TestTrue(TEXT("Cancel keeps mount, clears only action"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted && F.Riding->GetRidingState().Action == ERPGRidingAction::None);
    TestTrue(TEXT("No cooldown refund"), F.Session->GetOwnerState().DashEndServerSeconds >= Cooldown-.1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASLifetimeTest, "ProjectRPG.Riding.DashGAS.Lifetime", Flags)
bool FRidingDashGASLifetimeTest::RunTest(const FString&)
{
    {
        FFixture F; const auto H = F.Handle();
        if (!TestTrue(TEXT("Dash live before unpossess"), F.Riding->RequestDash() && F.Riding->IsServerDashActive() && F.Active(H))) return false;
        F.PC->UnPossess(); F.Riding->RefreshBinding();
        TestNull(TEXT("Unpossess withdraws original ASC spec"), F.Pawn->ASC->FindAbilitySpecFromHandle(H));
        TestFalse(TEXT("Unpossess leaves no motion/window"), F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
        TestFalse(TEXT("Unpossessed component rejects input"), F.Riding->RequestDash());
    }
    {
        FFixture F; const auto H = F.Handle();
        if (!TestTrue(TEXT("Dash live before avatar loss"), F.Riding->RequestDash() && F.Riding->IsServerDashActive() && F.Active(H))) return false;
        F.Pawn->ASC->ClearActorInfo();
        F.Riding->TickComponent(.1f, LEVELTICK_All, &F.Riding->PrimaryComponentTick);
        TestNull(TEXT("ASC avatar loss withdraws old grant"), F.Pawn->ASC->FindAbilitySpecFromHandle(H));
        TestFalse(TEXT("Avatar loss releases motion/window"), F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
    }
    {
        FFixture F; const auto H = F.Handle();
        if (!TestTrue(TEXT("Dash live before unregister"), F.Riding->RequestDash() && F.Riding->IsServerDashActive() && F.Active(H))) return false;
        F.Riding->UnregisterComponent();
        TestNull(TEXT("Unregister withdraws spec"), F.Pawn->ASC->FindAbilitySpecFromHandle(H));
        TestFalse(TEXT("Unregister releases motion/window"), F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASReentryTest, "ProjectRPG.Riding.DashGAS.Reentry", Flags)
bool FRidingDashGASReentryTest::RunTest(const FString&)
{
    {
        FFixture F; const auto H = F.Handle();
        if (!TestTrue(TEXT("Ready before admission callback"), F.Riding->CanRequestDash() && H.IsValid())) return false;
        const auto D = F.Riding->OnChanged.AddLambda([&](const auto& S) { if (S.Action == ERPGRidingAction::Dash) F.Pawn->ASC->CancelAbilityHandle(H); });
        F.Riding->RequestDash(); F.Riding->OnChanged.Remove(D);
        TestFalse(TEXT("Earlier admission subscriber cancellation ends GAS/motion/window"), F.Active(H) || F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
        TestTrue(TEXT("Admission callback clears action"), F.Riding->GetRidingState().Action == ERPGRidingAction::None);
    }
    {
        FFixture F; const auto H = F.Handle();
        if (!TestTrue(TEXT("Ready before window callback"), F.Riding->CanRequestDash() && H.IsValid())) return false;
        F.Move->OnOpen = [&] { F.Pawn->ASC->CancelAbilityHandle(H); };
        F.Riding->RequestDash();
        TestFalse(TEXT("Window callback cancellation closes acquired resource"), F.Active(H) || F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
        TestEqual(TEXT("Cancelled acquired window closed exactly once"), F.Move->Closed, 1);
    }
    {
        FFixture F; const auto H = F.Handle(); const auto OldEpoch = F.Riding->GetRidingState().Key.RiderEpoch;
        if (!TestTrue(TEXT("Dash live before grant-removal callback"), F.Riding->RequestDash() && F.Riding->IsServerDashActive() && F.Active(H))) return false;
        bool Rebound = false; FGuid NewEpoch;
        const auto D = F.Pawn->ASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Data)
        {
            if (Rebound || Data.AbilitySpecHandle != H) return;
            Rebound = true;
            F.Riding->RefreshBinding(); F.Riding->RequestSetMounted(true, {});
            NewEpoch = F.Riding->GetRidingState().Key.RiderEpoch;
        });
        FRPGRidingDashAbilityTestAccess::Release(*F.Riding);
        F.Pawn->ASC->OnAbilityEnded.Remove(D);
        TestTrue(TEXT("Removal callback establishes a fresh epoch"), Rebound && NewEpoch.IsValid() && NewEpoch != OldEpoch);
        TestTrue(TEXT("Old release preserves callback's mounted state"), F.Riding->GetRidingState().Key.RiderEpoch == NewEpoch && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
        TestTrue(TEXT("Only fresh grant survives the old release"), F.Handle().IsValid() && F.Handle() != H && !F.Pawn->ASC->FindAbilitySpecFromHandle(H));
        TestFalse(TEXT("Old release leaves no old motion/window"), F.Riding->IsServerDashActive() || F.Move->Window.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashGASDirectionTest, "ProjectRPG.Riding.DashGAS.DirectionAndRejection", Flags)
bool FRidingDashGASDirectionTest::RunTest(const FString&)
{
    {
        FFixture F; F.Pawn->SetActorRotation(FRotator(0,90,0));
        if (!TestTrue(TEXT("Facing Dash starts"), F.Riding->RequestDash() && F.Riding->IsServerDashActive())) return false;
        TestTrue(TEXT("Stationary Dash uses server facing"), FVector(F.Riding->GetRidingState().ActionDirection).Equals(FVector::RightVector, .01));
    }
    {
        FFixture F; F.Pawn->AddMovementInput(FVector::RightVector); F.Tick();
        if (!TestTrue(TEXT("Moving Dash starts"), F.Riding->RequestDash() && F.Riding->IsServerDashActive())) return false;
        TestTrue(TEXT("Accepted acceleration takes precedence over facing"), FVector(F.Riding->GetRidingState().ActionDirection).Equals(FVector::RightVector, .01));
    }
    {
        FFixture F; F.Box(FVector(F.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()+5,0,120), FVector(5,200,120));
        if (!TestTrue(TEXT("GAS admission ready before physical preflight"), F.Riding->CanRequestDash())) return false;
        const auto H = F.Handle(); F.Riding->RequestDash();
        TestFalse(TEXT("Blocked preflight leaves neither GAS nor motion"), F.Active(H) || F.Riding->IsServerDashActive());
        TestEqual(TEXT("Rejected start allocates no window"), F.Move->Opened, 0);
        TestTrue(TEXT("Rejected start does not spend Dash cooldown"), F.Session->GetOwnerState().DashEndServerSeconds <= F.World->GetTimeSeconds()+.1);
    }
    return true;
}
#endif
