#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGMountDefinition.h"
#include "Tests/RPGRidingActorTests.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingDashTestAccess
{
    static ERPGRidingResultCode Begin(URPGRidingComponent& C, FRPGRidingDashTicket& Out, FVector Direction = FVector::ForwardVector)
    { return C.TryBeginDash(C.State.Key, Direction, [](const auto&, const auto&) { return true; }, Out); }
    static ERPGRidingResultCode Preflight(URPGRidingComponent& C,
        TFunctionRef<bool(const FRPGRidingDashProfile&, const FVector&)> Check, FRPGRidingDashTicket& Out)
    { return C.TryBeginDash(C.State.Key, FVector::ForwardVector, Check, Out); }
    static bool Finish(URPGRidingComponent& C, const FRPGRidingDashTicket& T, ERPGRidingEndReason R = ERPGRidingEndReason::Completed)
    { return C.FinishDash(T, R); }
    static bool Current(URPGRidingComponent& C, const FRPGRidingDashTicket& T) { return C.IsDashCurrent(T); }
    static void Unprotect(URPGRidingSessionComponent& S) { S.Runtime->DashReadyAt = 0; }
    static double Remaining(URPGRidingSessionComponent& S) { return S.Runtime->GetTiming(FPlatformTime::Seconds()).DashRemaining; }
    static void Expire(URPGRidingComponent& C)
    { C.DashDeadline = FPlatformTime::Seconds() - 1; C.TickComponent(0.f, LEVELTICK_All, &C.PrimaryComponentTick); }
};
void RPGUnprotectRidingDashTestSession(URPGRidingSessionComponent& S) { FRPGRidingDashTestAccess::Unprotect(S); }
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    using FAccess = FRPGRidingDashTestAccess;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingTestPawn* Pawn;
        URPGRidingSessionComponent* Session;
        URPGRidingComponent* Riding;
        FFixture(bool Unlock = true)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Pawn = Spawn(); PC->Possess(Pawn);
            Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            if (Unlock) FAccess::Unprotect(*Session); // Only a dev fixture bypasses the real initial ten-second protection.
        }
        ARPGRidingTestPawn* Spawn()
        {
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Value = World->SpawnActor<ARPGRidingTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, P);
            Value->ASC->InitAbilityActorInfo(Value, Value); Value->ASC->AddAttributeSetSubobject(Value->Attributes.Get());
            Value->Attributes->CurrentHealth.SetBaseValue(100); Value->Attributes->CurrentHealth.SetCurrentValue(100);
            Value->GetCharacterMovement()->MovementMode = MOVE_Walking;
            return Value;
        }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Riding->GetRidingState().Mode == ERPGRidingMode::Mounted; }
        ~FFixture() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashAdmissionTest, "ProjectRPG.Riding.DashAdmission.Approval", Flags)
bool FRidingDashAdmissionTest::RunTest(const FString&)
{
    FFixture F(false); FRPGRidingDashTicket T;
    TestTrue(TEXT("On-foot cannot start action"), FAccess::Begin(*F.Riding, T) == ERPGRidingResultCode::NotReady);
    if (!TestTrue(TEXT("Mounted fixture"), F.Mount())) return false;
    TestTrue(TEXT("Initial admission protects dash"), FAccess::Begin(*F.Riding, T) == ERPGRidingResultCode::Busy);
    FAccess::Unprotect(*F.Session);
    TestTrue(TEXT("Zero direction rejected"), FAccess::Begin(*F.Riding, T, FVector::ZeroVector) == ERPGRidingResultCode::ActionBlocked);
    TestTrue(TEXT("Vertical direction rejected"), FAccess::Begin(*F.Riding, T, FVector(0,0,1)) == ERPGRidingResultCode::ActionBlocked);
    TestTrue(TEXT("Preflight rejection does not approve"), FAccess::Preflight(*F.Riding, [](const auto&, const auto&) { return false; }, T) == ERPGRidingResultCode::ActionBlocked);
    TestEqual(TEXT("Rejected preflight does not charge cooldown"), FAccess::Remaining(*F.Session), 0.0);
    const auto Before = F.Riding->GetRidingState().Key;
    if (!TestTrue(TEXT("Server approves dash"), FAccess::Begin(*F.Riding, T) == ERPGRidingResultCode::Applied)) return false;
    const auto State = F.Riding->GetRidingState();
    TestTrue(TEXT("Canonical published action"), RPGRiding::IsCanonical(State) && State.Action == ERPGRidingAction::Dash);
    TestEqual(TEXT("Admitted policy distance"), T.Profile.Distance, 900.f);
    TestEqual(TEXT("Admitted policy duration"), State.ActionDuration, .6f);
    TestEqual(TEXT("Action advances independently of mount revision"), State.Key.StateRevision, Before.StateRevision);
    TestEqual(TEXT("New action sequence"), State.Key.ActionSequence, Before.ActionSequence + 1);
    TestTrue(TEXT("Ticket current"), FAccess::Current(*F.Riding, T));
    FRPGRidingDashTicket Duplicate;
    TestTrue(TEXT("No second simultaneous action"), FAccess::Begin(*F.Riding, Duplicate) == ERPGRidingResultCode::Busy);
    TestFalse(TEXT("Rejected output cleared"), Duplicate.ExecutionId.IsValid());
    TestTrue(TEXT("Ten-second deadline published before return"), F.Session->GetOwnerState().DashEndServerSeconds >= 9.9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReentryTest, "ProjectRPG.Riding.DashAdmission.Reentry", Flags)
bool FRidingDashReentryTest::RunTest(const FString&)
{
    {
        FFixture F; F.Mount(); FRPGRidingDashTicket T;
        const auto Result = FAccess::Preflight(*F.Riding, [&](const auto&, const auto&) { F.Riding->ForceDismount(ERPGRidingEndReason::Manual); return true; }, T);
        TestTrue(TEXT("Preflight cannot approve its obsolete observation"), Result != ERPGRidingResultCode::Applied);
        TestFalse(TEXT("No execution on stale observation"), T.ExecutionId.IsValid());
        TestEqual(TEXT("Callback rejection does not charge"), FAccess::Remaining(*F.Session), 0.0);
    }
    {
        FFixture F; F.Mount(); FRPGRidingDashTicket T;
        ERPGRidingResultCode Nested = ERPGRidingResultCode::Applied;
        F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& S) { if (S.Action == ERPGRidingAction::Dash) F.Riding->ForceDismount(ERPGRidingEndReason::Hit); });
        const auto Result = FAccess::Preflight(*F.Riding, [&](const auto&, const auto&) { FRPGRidingDashTicket Inner; Nested = FAccess::Begin(*F.Riding, Inner); return true; }, T);
        TestTrue(TEXT("Nested admission rejected"), Nested == ERPGRidingResultCode::Busy);
        TestTrue(TEXT("Admission was committed before immediate callback cancellation"), Result == ERPGRidingResultCode::Applied && T.ExecutionId.IsValid());
        TestFalse(TEXT("Executor must not start a cancelled admission"), FAccess::Current(*F.Riding, T));
        TestTrue(TEXT("Accepted then cancelled retains cooldown"), FAccess::Remaining(*F.Session) >= 9.9);
        TestFalse(TEXT("Late completion cannot change OnFoot"), FAccess::Finish(*F.Riding, T));
        F.Riding->OnChanged.Clear();
    }
    {
        FFixture F; F.Mount(); FRPGRidingDashTicket T;
        const auto R = FAccess::Preflight(*F.Riding, [&](const auto&, const auto&) { F.Pawn->Attributes->CurrentHealth.SetCurrentValue(0); return true; }, T);
        TestTrue(TEXT("Health is rechecked after trusted preflight"), R == ERPGRidingResultCode::ActionBlocked);
        TestEqual(TEXT("No cooldown on failed health guard"), FAccess::Remaining(*F.Session), 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashEndingTest, "ProjectRPG.Riding.DashAdmission.Endings", Flags)
bool FRidingDashEndingTest::RunTest(const FString&)
{
    FFixture F; F.Mount(); FRPGRidingDashTicket T;
    if (!TestTrue(TEXT("Approved fixture"), FAccess::Begin(*F.Riding, T) == ERPGRidingResultCode::Applied)) return false;
    auto Wrong = T; Wrong.ExecutionId = FGuid::NewGuid();
    TestFalse(TEXT("Different execution cannot finish"), FAccess::Finish(*F.Riding, Wrong));
    Wrong = T; ++Wrong.Key.ActionSequence;
    TestFalse(TEXT("Different action sequence cannot finish"), FAccess::Finish(*F.Riding, Wrong));
    TestTrue(TEXT("Exact completion applies"), FAccess::Finish(*F.Riding, T));
    auto S = F.Riding->GetRidingState();
    TestTrue(TEXT("Completion preserves canonical mount/profile"), RPGRiding::IsCanonical(S) && S.Mode == ERPGRidingMode::Mounted && S.Action == ERPGRidingAction::None && S.Movement.Speed == 600.f);
    TestFalse(TEXT("Completion cannot replay"), FAccess::Finish(*F.Riding, T));
    TestTrue(TEXT("End never refunds"), FAccess::Remaining(*F.Session) >= 9.9);
    FAccess::Unprotect(*F.Session);
    FRPGRidingDashTicket New;
    TestTrue(TEXT("Later execution has fresh identity"), FAccess::Begin(*F.Riding, New) == ERPGRidingResultCode::Applied && New.ExecutionId != T.ExecutionId);
    TestFalse(TEXT("Old completion cannot end newer action"), FAccess::Finish(*F.Riding, T));
    FAccess::Expire(*F.Riding);
    TestTrue(TEXT("Server monotonic expiry closes only action"), F.Riding->GetRidingState().Action == ERPGRidingAction::None && F.Riding->GetRidingState().LastEndReason == ERPGRidingEndReason::Completed);
    TestTrue(TEXT("Expiry keeps cooldown"), FAccess::Remaining(*F.Session) >= 9.9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashLifetimeTest, "ProjectRPG.Riding.DashAdmission.Lifetime", Flags)
bool FRidingDashLifetimeTest::RunTest(const FString&)
{
    FFixture F; F.Mount(); FRPGRidingDashTicket T;
    auto* Definition = const_cast<URPGMountDefinition*>(F.Riding->GetPresentationDefinition());
    FAccess::Begin(*F.Riding, T);
    F.Riding->ForceDismount(ERPGRidingEndReason::Control);
    TestFalse(TEXT("Force dismount seals old execution"), FAccess::Finish(*F.Riding, T));
    F.PC->UnPossess(); F.Pawn = F.Spawn(); F.PC->Possess(F.Pawn);
    F.Riding = F.Pawn->FindComponentByClass<URPGRidingComponent>(); F.Riding->RefreshBinding();
    URPGMountDefinition* Definitions[] = {Definition}; RPGSetRidingVisualTestState(*F.Riding, F.Riding->GetRidingState(), Definitions);
    TestTrue(TEXT("Replacement can mount without resetting session"), F.Mount());
    FRPGRidingDashTicket New;
    TestTrue(TEXT("Same session replacement retains dash cooldown"), FAccess::Begin(*F.Riding, New) == ERPGRidingResultCode::Busy);
    TestFalse(TEXT("Old Pawn's completion cannot affect replacement"), FAccess::Finish(*F.Riding, T));
    F.Pawn->SetRole(ROLE_AutonomousProxy);
    TestTrue(TEXT("Client cannot approve server action"), FAccess::Begin(*F.Riding, New) == ERPGRidingResultCode::AuthorityRevoked);
    F.Pawn->SetRole(ROLE_Authority);
    return true;
}
#endif
