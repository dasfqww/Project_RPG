#include "Tests/RPGRidingTimedInteractionTests.h"
#include "Tests/RPGRidingActorTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Riding/RPGRidingTimedInteractionComponent.h"
#include "Riding/RPGRidingTimedInteractionLease.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"

ARPGRidingTimedTarget::ARPGRidingTimedTarget()
{ SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root"))); bReplicates = true; InteractableData.InteractionDuration = 2.f; }
bool ARPGRidingTimedTarget::CanApproveRidingTimedInteraction(APlayerController* Controller) const
{
    return bAllowed && IsValid(Controller) && IsValid(Controller->GetPawn())
        && FVector::DistSquared(Controller->GetPawn()->GetActorLocation(), GetActorLocation()) <= 40000;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingTimedInteractionTestAccess
{
    static void Elapse(URPGRidingTimedInteractionComponent& C) { C.Server.Started = FPlatformTime::Seconds() - C.Server.Duration - .01; }
    static void ExpireServer(URPGRidingTimedInteractionComponent& C) { C.Server.Expires = FPlatformTime::Seconds() - 1; C.PollServer(); }
    static void ExpireLocal(URPGRidingTimedInteractionComponent& C) { C.LocalDeadline = FPlatformTime::Seconds() - 1; C.PollLocal(); }
    static void Poll(URPGRidingTimedInteractionComponent& C) { C.PollServer(); C.PollLocal(); }
    static uint32 Sequence(const URPGRidingTimedInteractionComponent& C) { return C.LocalSequence; }
    static void ResetBudget(URPGRidingTimedInteractionComponent& C) { C.NextServerRequest = 0; }
    static TSharedPtr<FRPGRidingTimedInteractionLease> Pin(URPGRidingTimedInteractionComponent& C) { return C.Lease; }
    static void CancelServer(URPGRidingTimedInteractionComponent& C, uint32 Seq) { C.ServerCancel_Implementation(Seq); }
    static void FinishServer(URPGRidingTimedInteractionComponent& C, uint32 Seq) { C.ServerFinish_Implementation(Seq); }
    static bool Prepare(URPGRidingTimedInteractionComponent& C, AActor& T, TFunction<void(float)> Ready, TFunction<void()> Done, TFunction<void()> Stop)
    { return C.Prepare(T, MoveTemp(Ready), MoveTemp(Done), MoveTemp(Stop)); }
    static void Started(URPGRidingTimedInteractionComponent& C, uint32 Seq, const FRPGRidingStateKey& Key, float Duration = 2)
    { C.ClientStarted_Implementation(Seq, true, Key, Duration); }
    static void Finished(URPGRidingTimedInteractionComponent& C, uint32 Seq, const FRPGRidingStateKey& Key)
    { C.ClientFinished_Implementation(Seq, true, Key); }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingTestPawn* Pawn;
        ARPGRidingTimedTarget* Target;
        URPGRidingComponent* Riding;
        URPGRidingTimedInteractionComponent* Timed;
        int32 Ready = 0, Complete = 0, Interrupted = 0;
        float ApprovedDuration = 0;
        TFunction<void()> Completing;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Pawn = World->SpawnActor<ARPGRidingTestPawn>(); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            Target = World->SpawnActor<ARPGRidingTimedTarget>(); Target->SetActorLocation(Pawn->GetActorLocation());
            Timed = NewObject<URPGRidingTimedInteractionComponent>(PC); PC->AddInstanceComponent(Timed); Timed->RegisterComponent();
        }
        ~FFixture() { if (IsValid(Pawn)) Pawn->SetRole(ROLE_Authority); RPGTestWorld::Destroy(World); }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Riding->GetRidingState().Mode == ERPGRidingMode::Mounted; }
        bool Begin() { return Timed->Begin(*Target, [&](float D) { ++Ready; ApprovedDuration = D; }, [&] { ++Complete; if (Completing) Completing(); }, [&] { ++Interrupted; }); }
        bool PrepareClient()
        {
            Pawn->SetRole(ROLE_AutonomousProxy);
            return FRPGRidingTimedInteractionTestAccess::Prepare(*Timed, *Target, [&](float D) { ++Ready; ApprovedDuration = D; }, [&] { ++Complete; }, [&] { ++Interrupted; });
        }
        FRPGRidingRepState Foot() const
        { FRPGRidingRepState State; State.Key = Riding->GetRidingState().Key; if (Riding->GetRidingState().Mode == ERPGRidingMode::Mounted) ++State.Key.StateRevision; return State; }
        void State(const FRPGRidingRepState& S) { RPGSetRidingVisualTestState(*Riding, S, {}); }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedLifetime, "ProjectRPG.Riding.TimedInteraction.ServerLifetime", Flags)
bool FRidingTimedLifetime::RunTest(const FString&)
{
    FFixture F; if (!TestTrue(TEXT("Mounted original Pawn"), F.Mount())) return false;
    TestTrue(TEXT("Owned start approval"), F.Begin());
    TestTrue(TEXT("Server dismounts and reserves original Pawn before progress"), F.Ready == 1 && F.Timed->IsActive()
        && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot && F.ApprovedDuration == 2 && FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    TestFalse(TEXT("Actual server mount is blocked for entire hold"), F.Mount());
    F.Completing = [&] { TestFalse(TEXT("Completion callback is pinned against reentrant mount"), F.Mount()); };
    FRPGRidingTimedInteractionTestAccess::Elapse(*F.Timed);
    TestTrue(TEXT("Elapsed server observation completes"), F.Timed->Finish());
    TestTrue(TEXT("One completion releases request and reservation"), F.Complete == 1 && F.Interrupted == 0
        && !F.Timed->HasRequest() && !FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    TestTrue(TEXT("Fresh mount succeeds after completion"), F.Mount());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedTokens, "ProjectRPG.Riding.TimedInteraction.CancellationTokens", Flags)
bool FRidingTimedTokens::RunTest(const FString&)
{
    FFixture F; F.Begin(); const auto Old = FRPGRidingTimedInteractionTestAccess::Sequence(*F.Timed);
    F.Timed->Cancel(); TestTrue(TEXT("Cancellation releases immediately on authority"), F.Interrupted == 1 && !FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    FRPGRidingTimedInteractionTestAccess::ResetBudget(*F.Timed); F.Begin();
    FRPGRidingTimedInteractionTestAccess::CancelServer(*F.Timed, Old); FRPGRidingTimedInteractionTestAccess::FinishServer(*F.Timed, Old);
    TestTrue(TEXT("Old cancel/finish cannot release newer hold"), F.Timed->IsActive() && FRPGRidingTimedInteractionLease::HasPending(F.Pawn) && F.Complete == 0);
    auto Pin = FRPGRidingTimedInteractionTestAccess::Pin(*F.Timed); F.Timed->Cancel();
    TestFalse(TEXT("A live callback pin retains mounting barrier"), F.Mount()); Pin.Reset();
    TestTrue(TEXT("Last original token release permits mount"), F.Mount()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedPreflight, "ProjectRPG.Riding.TimedInteraction.PreflightAndEarlyFinish", Flags)
bool FRidingTimedPreflight::RunTest(const FString&)
{
    {
        FFixture F; F.Mount(); F.Target->bAllowed = false;
        TestFalse(TEXT("Invalid target rejected before dismount"), F.Begin()); F.Target->bAllowed = true;
        F.Target->InteractableData.InteractionDuration = 61;
        TestFalse(TEXT("Unbounded authored duration rejected"), F.Begin());
        TestTrue(TEXT("Rejected preparation preserves mounted state"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted && !FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    }
    {
        FFixture F; F.Mount();
        const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
            { if (State.Mode == ERPGRidingMode::OnFoot) F.Target->Identity = FGuid::NewGuid(); });
        F.Begin(); F.Riding->OnChanged.Remove(Handle);
        TestTrue(TEXT("Dismount callbacks cannot replace the approved target generation"), F.Ready == 0 && F.Interrupted == 1
            && !F.Timed->HasRequest() && !FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    }
    {
        FFixture F; F.Begin(); F.Timed->Finish();
        TestTrue(TEXT("Client cannot complete before server duration elapsed"), F.Complete == 0 && F.Interrupted == 1
            && !F.Timed->HasRequest() && !FRPGRidingTimedInteractionLease::HasPending(F.Pawn));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedInvalidation, "ProjectRPG.Riding.TimedInteraction.ServerInvalidation", Flags)
bool FRidingTimedInvalidation::RunTest(const FString&)
{
    for (int32 Case = 0; Case < 9; ++Case)
    {
        FFixture F; F.Begin(); const auto Seq = FRPGRidingTimedInteractionTestAccess::Sequence(*F.Timed);
        switch (Case)
        {
        case 0: F.Target->Destroy(); break;
        case 1: F.Target->Identity = FGuid::NewGuid(); break;
        case 2: F.Target->SetActorLocation(FVector(500, 0, 0)); break;
        case 3: F.Target->InteractableData.InteractionDuration = 3; break;
        case 4: F.Pawn->Attributes->CurrentHealth.SetCurrentValue(0); break;
        case 5: F.PC->UnPossess(); F.PC->Possess(F.Pawn); break;
        case 6: F.Timed->UnregisterComponent(); break;
        case 7: FRPGRidingTimedInteractionTestAccess::ExpireServer(*F.Timed); break;
        default: F.Timed->EndPlay(EEndPlayReason::Destroyed); break;
        }
        FRPGRidingTimedInteractionTestAccess::Poll(*F.Timed); FRPGRidingTimedInteractionTestAccess::FinishServer(*F.Timed, Seq);
        TestTrue(TEXT("Target/generation/range/duration/death/Pawn/component/expiry teardown releases only old hold"),
            !FRPGRidingTimedInteractionLease::HasPending(F.Pawn) && !F.Timed->HasRequest() && F.Complete == 0 && F.Interrupted == 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedOrdering, "ProjectRPG.Riding.TimedInteraction.ClientOrdering", Flags)
bool FRidingTimedOrdering::RunTest(const FString&)
{
    for (bool StateFirst : {false, true})
    {
        FFixture F; F.Mount(); const auto Foot = F.Foot(); F.PrepareClient(); const auto Seq = FRPGRidingTimedInteractionTestAccess::Sequence(*F.Timed);
        if (StateFirst) F.State(Foot);
        FRPGRidingTimedInteractionTestAccess::Started(*F.Timed, Seq, Foot.Key);
        if (!StateFirst)
        {
            TestTrue(TEXT("Start receipt alone does not start progress or write state"), F.Ready == 0 && !F.Timed->IsActive() && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
            F.State(Foot); FRPGRidingTimedInteractionTestAccess::Poll(*F.Timed);
        }
        TestTrue(TEXT("Either state/receipt order starts one approved progress"), F.Ready == 1 && F.Timed->IsActive());
        F.Timed->Finish(); FRPGRidingTimedInteractionTestAccess::Finished(*F.Timed, Seq, Foot.Key);
        FRPGRidingTimedInteractionTestAccess::Finished(*F.Timed, Seq, Foot.Key);
        TestTrue(TEXT("Repeated completion invokes original continuation once"), F.Complete == 1 && !F.Timed->HasRequest());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedClientCancel, "ProjectRPG.Riding.TimedInteraction.ClientCancellation", Flags)
bool FRidingTimedClientCancel::RunTest(const FString&)
{
    FFixture F; F.Mount(); const auto Foot = F.Foot(); F.PrepareClient(); const auto Old = FRPGRidingTimedInteractionTestAccess::Sequence(*F.Timed);
    F.Timed->Cancel(); F.PrepareClient(); const auto New = FRPGRidingTimedInteractionTestAccess::Sequence(*F.Timed);
    FRPGRidingTimedInteractionTestAccess::Started(*F.Timed, Old, Foot.Key); TestTrue(TEXT("Old receipt cannot start newer input"), F.Ready == 0 && F.Timed->HasRequest());
    FRPGRidingTimedInteractionTestAccess::ExpireLocal(*F.Timed);
    FRPGRidingTimedInteractionTestAccess::Started(*F.Timed, New, Foot.Key);
    TestTrue(TEXT("Release/start timeout cannot be resurrected"), F.Ready == 0 && F.Complete == 0 && F.Interrupted == 2 && !F.Timed->HasRequest()); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingTimedWire, "ProjectRPG.Riding.TimedInteraction.Wire", Flags)
bool FRidingTimedWire::RunTest(const FString&)
{
    const auto* Type = URPGRidingTimedInteractionComponent::StaticClass();
    for (const TCHAR* Name : {TEXT("ServerBegin"), TEXT("ServerFinish"), TEXT("ServerCancel")})
    {
        const auto* Fn = Type->FindFunctionByName(FName(Name));
        TestTrue(TEXT("Owned commands are reflected reliable server RPCs"), Fn && Fn->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
        TestFalse(TEXT("Client supplies no execution duration"), Fn && FindFProperty<FProperty>(Fn, TEXT("InDuration")));
    }
    const auto* Begin = Type->FindFunctionByName(TEXT("ServerBegin"));
    TestTrue(TEXT("Original epoch/revision is reflected"), Begin && FindFProperty<FStructProperty>(Begin, TEXT("InExpected")));
    const auto* Ack = Type->FindFunctionByName(TEXT("ClientStarted"));
    TestTrue(TEXT("Server duration is delivered only in owner receipt"), Ack && Ack->HasAllFunctionFlags(FUNC_Net | FUNC_NetClient | FUNC_NetReliable)
        && FindFProperty<FFloatProperty>(Ack, TEXT("InDuration")));
    return true;
}
#endif
