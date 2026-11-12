#include "Riding/RPGRidingInteractionScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Tests/RPGRidingActorTests.h"
#include "Tests/RPGRidingDashMotionTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformProcess.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingDashTestPawn* Pawn;
        AActor* Target;
        URPGRidingComponent* Riding;
        int32 Generation = 1, Executed = 0;
        uint64 Frame = 0;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(true);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            auto* Floor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box); Box->SetRelativeLocation(FVector(0, 0, -50));
            Box->SetMobility(EComponentMobility::Static); Box->SetBoxExtent(FVector(3000, 3000, 50));
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingDashTestPawn>(FVector(0, 0, 200), FRotator::ZeroRotator, Params); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            RPGUnprotectRidingDashTestSession(*Session);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
            for (int32 I = 0; I < 40; ++I)
            {
                Frame = FMath::Max(Frame, GFrameCounter) + 1; TGuardValue<uint64> Guard(GFrameCounter, Frame);
                World->Tick(LEVELTICK_All, 1.f / 60);
            }
            // Give the otherwise empty actor a root so range mutations have real world positions.
            Target = World->SpawnActor<AActor>(); auto* Root = NewObject<USceneComponent>(Target);
            Target->SetRootComponent(Root); Target->AddInstanceComponent(Root); Root->RegisterComponent();
            Target->SetActorLocation(Pawn->GetActorLocation());
        }
        ~FFixture() { RPGTestWorld::Destroy(World); }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Riding->GetRidingState().Mode == ERPGRidingMode::Mounted; }
        bool Validate() const
        {
            return IsValid(Target) && Generation == 1
                && FVector::DistSquared(Pawn->GetActorLocation(), Target->GetActorLocation()) <= 10000;
        }
        bool Execute() { return FRPGRidingInteractionScope::TryExecute(*Pawn, *Target, [&] { return Validate(); }, [&] { ++Executed; return true; }); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionValidation, "ProjectRPG.Riding.Interaction.ValidationBeforeDismount", Flags)
bool FRidingInteractionValidation::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    const auto Before = F.Riding->GetRidingState();
    F.Target->SetActorLocation(FVector(500, 0, 0));
    TestFalse(TEXT("Out-of-range interaction rejected"), F.Execute());
    F.Target->SetActorLocation(FVector::ZeroVector); F.Generation = 2;
    TestFalse(TEXT("Wrong original target generation rejected"), F.Execute());
    TestTrue(TEXT("Preflight rejection preserves mounted state and revision"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted
        && F.Riding->GetRidingState().Key.StateRevision == Before.Key.StateRevision && F.Executed == 0);
    TestFalse(TEXT("Rejected operation retains no barrier"), FRPGRidingInteractionScope::HasPending(F.Pawn));
    F.Generation = 1; F.Pawn->SetRole(ROLE_AutonomousProxy);
    TestFalse(TEXT("Client cannot dismount or execute authoritatively"), F.Execute());
    F.Pawn->SetRole(ROLE_Authority);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionExecution, "ProjectRPG.Riding.Interaction.DismountAndSingleExecution", Flags)
bool FRidingInteractionExecution::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    const auto Epoch = F.Riding->GetRidingState().Key.RiderEpoch;
    bool CallbackGuard = false, Nested = true, Remounted = true;
    const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    {
        if (State.Mode != ERPGRidingMode::OnFoot) return;
        CallbackGuard = FRPGRidingInteractionScope::HasPending(F.Pawn);
        Nested = F.Execute(); Remounted = F.Mount();
    });
    int32 Checks = 0;
    const bool Applied = FRPGRidingInteractionScope::TryExecute(*F.Pawn, *F.Target,
        [&] { ++Checks; return F.Validate(); }, [&]
        {
            TestTrue(TEXT("Original Pawn stays possessed and on foot before execution"), F.PC->GetPawn() == F.Pawn
                && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot && F.Riding->GetRidingState().Key.RiderEpoch == Epoch);
            TestFalse(TEXT("Execution callback cannot remount"), F.Mount());
            ++F.Executed; return true;
        });
    F.Riding->OnChanged.Remove(Handle);
    TestTrue(TEXT("Operation accepted after two successful checks"), Applied && Checks == 2 && F.Executed == 1);
    TestTrue(TEXT("Dismount callback sees reservation"), CallbackGuard);
    TestFalse(TEXT("Nested interaction cannot run twice"), Nested);
    TestFalse(TEXT("Dismount callback cannot overtake with a mount"), Remounted);
    TestFalse(TEXT("Synchronous operation releases its reservation"), FRPGRidingInteractionScope::HasPending(F.Pawn));
    TestTrue(TEXT("A fresh mount is allowed after observation was accepted"), F.Mount());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionTargetChange, "ProjectRPG.Riding.Interaction.TargetChangeAfterDismount", Flags)
bool FRidingInteractionTargetChange::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
        { if (State.Mode == ERPGRidingMode::OnFoot) F.Target->SetActorLocation(FVector(500, 0, 0)); });
    TestFalse(TEXT("Distance rechecked after actual dismount callbacks"), F.Execute());
    F.Riding->OnChanged.Remove(Handle); F.Target->SetActorLocation(FVector::ZeroVector);
    TestTrue(TEXT("Can mount after aborted request"), F.Mount());
    Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
        { if (State.Mode == ERPGRidingMode::OnFoot) ++F.Generation; });
    TestFalse(TEXT("Pooled target or connection generation cannot be replaced during dismount"), F.Execute());
    F.Riding->OnChanged.Remove(Handle);
    TestTrue(TEXT("Neither invalidated request executed"), F.Executed == 0 && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionIdentity, "ProjectRPG.Riding.Interaction.PossessionAndEpoch", Flags)
bool FRidingInteractionIdentity::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    const auto Epoch = F.Riding->GetRidingState().Key.RiderEpoch;
    const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    {
        if (State.Mode == ERPGRidingMode::OnFoot && State.Key.RiderEpoch == Epoch)
        {
            F.PC->UnPossess(); F.PC->Possess(F.Pawn);
        }
    });
    TestFalse(TEXT("Same Pawn with a replaced possession epoch cannot inherit request"), F.Execute());
    F.Riding->OnChanged.Remove(Handle);
    TestTrue(TEXT("Binding actually changed"), F.Riding->GetRidingState().Key.RiderEpoch != Epoch);
    TestEqual(TEXT("Old epoch never executes"), F.Executed, 0);
    TestFalse(TEXT("Old reservation released after cancellation"), FRPGRidingInteractionScope::HasPending(F.Pawn));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionTransition, "ProjectRPG.Riding.Interaction.TransitionReentry", Flags)
bool FRidingInteractionTransition::RunTest(const FString&)
{
    FFixture F; bool Attempted = false, Accepted = true;
    const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    {
        if (State.Mode == ERPGRidingMode::Mounted) { Attempted = true; Accepted = F.Execute(); }
    });
    TestTrue(TEXT("Mount publication completes normally"), F.Mount());
    F.Riding->OnChanged.Remove(Handle);
    TestTrue(TEXT("Reentrant request was attempted"), Attempted);
    TestFalse(TEXT("Busy transition cannot enqueue a dismount or interaction"), Accepted);
    TestTrue(TEXT("No deferred work changes the completed mount"), F.Executed == 0
        && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted && !FRPGRidingInteractionScope::HasPending(F.Pawn));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionCleanup, "ProjectRPG.Riding.Interaction.FailureAndLifetime", Flags)
bool FRidingInteractionCleanup::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    TestFalse(TEXT("Definitive execution rejection is propagated"), FRPGRidingInteractionScope::TryExecute(*F.Pawn, *F.Target,
        [&] { return F.Validate(); }, [&] { ++F.Executed; return false; }));
    TestEqual(TEXT("No automatic retry of failed operation"), F.Executed, 1);
    TestTrue(TEXT("Failure releases guard"), F.Mount());
    const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
        { if (State.Mode == ERPGRidingMode::OnFoot) F.Target->Destroy(); });
    TestFalse(TEXT("Destroyed target never reaches validation or execution again"), F.Execute());
    F.Riding->OnChanged.Remove(Handle);
    TestEqual(TEXT("Destroyed target did not execute"), F.Executed, 1);
    TestFalse(TEXT("Target destruction releases guard"), FRPGRidingInteractionScope::HasPending(F.Pawn));
    auto* BarePawn = F.World->SpawnActor<APawn>(); auto* BareTarget = F.World->SpawnActor<AActor>(); F.PC->Possess(BarePawn);
    TestTrue(TEXT("Existing server interactions without a Riding component remain supported"),
        FRPGRidingInteractionScope::TryExecute(*BarePawn, *BareTarget, [] { return true; }, [] { return true; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingInteractionActions, "ProjectRPG.Riding.Interaction.ActiveActions", Flags)
bool FRidingInteractionActions::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    // The session's initial Emote protection uses monotonic real time, not the
    // simulated world ticks used to settle this Pawn. Keep that production guard.
    FPlatformProcess::SleepNoStats(2.1f);
    if (!TestTrue(TEXT("Actual Emote GAS activation"), F.Riding->RequestEmote())) return false;
    TestTrue(TEXT("Interaction ends Emote before execution"), F.Execute()
        && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot && F.Riding->GetRidingState().Action == ERPGRidingAction::None);
    TestTrue(TEXT("Remount for Dash"), F.Mount());
    if (!TestEqual(TEXT("Actual server Dash activation"), F.Riding->StartServerDash(F.Riding->GetRidingState().Key, FVector::ForwardVector), ERPGRidingResultCode::Applied)) return false;
    auto* Move = CastChecked<URPGRidingDashTestMovement>(F.Pawn->GetCharacterMovement());
    if (!TestTrue(TEXT("Dash has live motion and its own authorization window"), F.Riding->IsServerDashActive() && Move->Window.IsValid())) return false;
    TestTrue(TEXT("Interaction also executes after Dash dismount"), F.Execute());
    TestTrue(TEXT("Dash motion and window are released before returning"), !F.Riding->IsServerDashActive()
        && !Move->Window.IsValid() && Move->Closed == Move->Opened && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestEqual(TEXT("Each accepted interaction executes once"), F.Executed, 2);
    return true;
}
#endif
