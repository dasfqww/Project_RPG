#include "Riding/RPGRidingDashRootMotion.h"
#include "Riding/RPGRidingMovementPrediction.h"
#include "Riding/RPGRidingComponent.h"
#include "Tests/RPGRidingActorTests.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FRPGRidingRepState Dash()
    {
        FRPGRidingRepState S; S.Key = {FGuid::NewGuid(), 1, 1}; S.Mode = ERPGRidingMode::Mounted;
        S.MountId = TEXT("horse"); S.DefinitionVersion = 1; S.Movement = {1,600,2048,2000,500};
        S.Action = ERPGRidingAction::Dash; S.ExecutionId = FGuid::NewGuid(); S.ActionDuration = .6f;
        S.ActionDirection = FVector::ForwardVector; S.ActionStartServerWorldSeconds = 1;
        return S;
    }
    TSharedPtr<FRootMotionSource_RPGRidingDash> Source(const FRPGRidingRepState& S)
    {
        auto R = MakeShared<FRootMotionSource_RPGRidingDash>(); R->RidingKey = S.Key; R->RidingExecution = S.ExecutionId;
        R->InstanceName = TEXT("RidingReplay"); R->Force = FVector(1500,0,0); R->Duration = .6f;
        return R;
    }
    FRPGRidingRepState Foot(const FRPGRidingRepState& Old)
    { FRPGRidingRepState S; S.Key = Old.Key; ++S.Key.StateRevision; ++S.Key.ActionSequence; return S; }
    struct FFixture
    {
        UWorld* World;
        ARPGRidingTestPawn* Pawn;
        URPGRidingComponent* Riding;
        UCharacterMovementComponent* Move;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            Pawn = World->SpawnActor<ARPGRidingTestPawn>(); Riding = Pawn->FindComponentByClass<URPGRidingComponent>();
            Move = Pawn->GetCharacterMovement(); Move->SetMovementMode(MOVE_Walking); Move->MaxWalkSpeed = 400;
        }
        void State(const FRPGRidingRepState& S) { RPGSetRidingVisualTestState(*Riding, S, {}); }
        ~FFixture() { Pawn->SetRole(ROLE_Authority); World->DestroyWorld(false); }
    };
    struct FReentrySource : FRootMotionSource_ConstantForce
    {
        TFunction<void()> OnUpdate;
        virtual bool UpdateStateFrom(const FRootMotionSource* Other, bool Catchup = false) override
        {
            const bool Result = FRootMotionSource_ConstantForce::UpdateStateFrom(Other, Catchup);
            auto Callback = MoveTemp(OnUpdate); OnUpdate = nullptr;
            if (Callback) Callback();
            return Result;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReplayIdentity, "ProjectRPG.Riding.DashReplay.Identity", Flags)
bool FRidingDashReplayIdentity::RunTest(const FString&)
{
    const auto S = Dash(); auto R = Source(S);
    TestTrue(TEXT("Canonical fixture/current identity"), RPGRiding::IsCanonical(S) && R->IsCurrent(S));
    TUniquePtr<FRootMotionSource> Copy(R->Clone());
    TestTrue(TEXT("Clone preserves native type and matching identity"), Copy->GetScriptStruct() == R->GetScriptStruct() && R->Matches(Copy.Get()));
    auto& C = static_cast<FRootMotionSource_RPGRidingDash&>(*Copy);
    C.RidingExecution = FGuid::NewGuid(); TestFalse(TEXT("Different execution does not match"), R->Matches(&C));
    C = *R; ++C.RidingKey.ActionSequence; TestFalse(TEXT("Different action does not match"), R->Matches(&C));
    C = *R; ++C.RidingKey.StateRevision; TestFalse(TEXT("Different mount does not match"), R->Matches(&C));
    C = *R; C.RidingKey.RiderEpoch = FGuid::NewGuid(); TestFalse(TEXT("Different Pawn epoch does not match"), R->Matches(&C));
    FRootMotionSource_ConstantForce Foreign; Foreign.InstanceName = R->InstanceName; Foreign.Force = R->Force; Foreign.Duration = R->Duration;
    TestFalse(TEXT("Same name does not grant foreign source Riding ownership"), R->Matches(&Foreign));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReplayWire, "ProjectRPG.Riding.DashReplay.Wire", Flags)
bool FRidingDashReplayWire::RunTest(const FString&)
{
    auto R = Source(Dash()); TArray<uint8> Bytes; bool Success = false;
    FMemoryWriter Writer(Bytes); FObjectAndNameAsStringProxyArchive Out(Writer, false);
    TestTrue(TEXT("Native source serialization"), R->NetSerialize(Out, nullptr, Success) && Success && !Out.IsError());
    FRootMotionSource_RPGRidingDash Copy;
    FMemoryReader Reader(Bytes); FObjectAndNameAsStringProxyArchive In(Reader, false);
    TestTrue(TEXT("Native source deserialization"), Copy.NetSerialize(In, nullptr, Success) && Success && !In.IsError());
    TestTrue(TEXT("Wire preserves identity and force"), R->Matches(&Copy) && Copy.Force.Equals(R->Force));
    TestTrue(TEXT("Wire reconstruction retains vertical/gravity semantics"), Copy.Settings.HasFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate));
    for (int32 Case = 0; Case < 3; ++Case)
    {
        auto Bad = Source(Dash());
        if (Case == 0) Bad->RidingExecution.Invalidate();
        if (Case == 1) Bad->RidingKey.ActionSequence = 0;
        if (Case == 2) Bad->bInLocalSpace = true;
        TArray<uint8> BadBytes; FMemoryWriter W(BadBytes); FObjectAndNameAsStringProxyArchive A(W, false);
        TestFalse(TEXT("Malformed identity/frame fails closed"), Bad->NetSerialize(A, nullptr, Success));
        TestTrue(TEXT("Failure propagates to archive and success flag"), A.IsError() && !Success);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReplayOwnership, "ProjectRPG.Riding.DashReplay.Ownership", Flags)
bool FRidingDashReplayOwnership::RunTest(const FString&)
{
    auto Old = Dash(); auto Current = Old; ++Current.Key.ActionSequence; Current.ExecutionId = FGuid::NewGuid();
    auto Stale = Source(Old); auto Fresh = Source(Current);
    auto Foreign = MakeShared<FRootMotionSource_ConstantForce>(); Foreign->InstanceName = Stale->InstanceName;
    Foreign->AccumulateMode = ERootMotionAccumulateMode::Additive; Foreign->SetTime(.2f);
    auto PendingForeign = MakeShared<FRootMotionSource_ConstantForce>();
    FRootMotionSourceGroup G; G.RootMotionSources = {Stale, Fresh, Foreign}; G.PendingAddRootMotionSources = {Source(Old), PendingForeign};
    G.bIsAdditiveVelocityApplied = true; G.LastPreAdditiveVelocity = FVector(23,0,-11);
    TestTrue(TEXT("Stale current and pending Dash removed"), RPGRidingDashMotion::RejectStale(G, Current));
    TestTrue(TEXT("Fresh execution and exact same-name foreign source preserved"), G.RootMotionSources.Num() == 2 && G.RootMotionSources.Contains(Fresh) && G.RootMotionSources.Contains(Foreign));
    TestTrue(TEXT("Foreign pending source preserved"), G.PendingAddRootMotionSources.Num() == 1 && G.PendingAddRootMotionSources[0] == PendingForeign);
    TestTrue(TEXT("Cached velocity modes rebuilt from retained sources"), G.HasOverrideVelocity() && G.HasAdditiveVelocity() && G.HasOverrideVelocityWithIgnoreZAccumulate());
    TestTrue(TEXT("Foreign additive history and clock untouched"), FVector(G.LastPreAdditiveVelocity).Equals(FVector(23,0,-11)) && G.bIsAdditiveVelocityApplied && FMath::IsNearlyEqual(Foreign->GetTime(), .2f));
    RPGRidingDashMotion::RejectStale(G, Foot(Current));
    TestTrue(TEXT("Dismount retires only Riding override"), G.RootMotionSources.Num() == 1 && G.RootMotionSources[0] == Foreign && !G.HasOverrideVelocity() && G.HasAdditiveVelocity());
    TestFalse(TEXT("No further mutation with only foreign sources"), RPGRidingDashMotion::RejectStale(G, {}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReplaySavedMove, "ProjectRPG.Riding.DashReplay.SavedMove", Flags)
bool FRidingDashReplaySavedMove::RunTest(const FString&)
{
    FFixture F; const auto Old = Dash(); F.State(Foot(Old));
    auto Stale = Source(Old); Stale->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
    Stale->FinishVelocityParams.SetVelocity = FVector(99999,0,99999);
    FSavedMovePtr Saved(new FSavedMove_RPGRiding()); auto* Data = F.Move->GetPredictionData_Client_Character(); Data->SavedMoves.Add(Saved);
    Saved->SavedRootMotion.RootMotionSources.Add(Stale); F.Pawn->SavedRootMotion = Saved->SavedRootMotion; F.Move->CurrentRootMotion = Saved->SavedRootMotion;
    F.Move->Velocity = FVector(1500,0,123); F.Pawn->bClientResimulateRootMotionSources = true;
    Saved->PrepMoveFor(F.Pawn);
    TestFalse(TEXT("Engine replay cannot resurrect old Riding source"), Saved->SavedRootMotion.HasActiveRootMotionSources() || F.Pawn->SavedRootMotion.HasActiveRootMotionSources() || F.Move->CurrentRootMotion.HasActiveRootMotionSources());
    TestTrue(TEXT("No stale finish velocity; current foot speed and external Z preserved"), F.Move->Velocity.Size2D() <= 400.01 && F.Move->Velocity.Z == 123);
    auto Fresh = Dash(); F.State(Fresh); auto Foreign = MakeShared<FRootMotionSource_ConstantForce>(); Foreign->Duration = 1;
    Saved->SavedRootMotion.RootMotionSources = {Source(Fresh), Foreign}; F.Pawn->SavedRootMotion = Saved->SavedRootMotion;
    F.Pawn->bClientResimulateRootMotionSources = false; Saved->PrepMoveFor(F.Pawn);
    TestEqual(TEXT("Live Riding and foreign source remain replayable"), F.Move->CurrentRootMotion.RootMotionSources.Num(), 2);
    TestTrue(TEXT("Replay never rewrites current authority state"), F.Riding->GetRidingState().ExecutionId == Fresh.ExecutionId);
    {
        FFixture Reentry; const auto Before = Dash(); Reentry.State(Before);
        FSavedMovePtr Replay(new FSavedMove_RPGRiding()); Reentry.Move->GetPredictionData_Client_Character()->SavedMoves.Add(Replay);
        Replay->DeltaTime = 1.f/60;
        auto ForeignCallback = MakeShared<FReentrySource>(); ForeignCallback->LocalID = 7; ForeignCallback->Duration = 10;
        ForeignCallback->OnUpdate = [&] { Reentry.State(Foot(Before)); };
        Replay->SavedRootMotion.RootMotionSources = {Source(Before), ForeignCallback};
        Reentry.Pawn->SavedRootMotion.RootMotionSources = {MakeShared<FReentrySource>(*ForeignCallback)};
        Reentry.Pawn->bClientResimulateRootMotionSources = true;
        Replay->PrepMoveFor(Reentry.Pawn);
        TestTrue(TEXT("Native foreign resimulation callback changed authority state"), Reentry.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
        // Engine group assignment clones sources; ownership is LocalID/type/state, not shared-pointer equality.
        TestEqual(TEXT("Post-replay current group retains only foreign source"), Reentry.Move->CurrentRootMotion.RootMotionSources.Num(), 1);
        TestEqual(TEXT("Post-replay saved group retains only foreign source"), Replay->SavedRootMotion.RootMotionSources.Num(), 1);
        if (Reentry.Move->CurrentRootMotion.RootMotionSources.Num() == 1)
            TestTrue(TEXT("Latest-state guard preserves the cloned foreign execution"), Reentry.Move->CurrentRootMotion.RootMotionSources[0]->LocalID == 7 && Reentry.Move->CurrentRootMotion.RootMotionSources[0]->GetScriptStruct() == FRootMotionSource_ConstantForce::StaticStruct());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDashReplayRepNotify, "ProjectRPG.Riding.DashReplay.RepNotify", Flags)
bool FRidingDashReplayRepNotify::RunTest(const FString&)
{
    FFixture F; auto Current = Dash(); F.State(Current); F.Pawn->SetRole(ROLE_AutonomousProxy);
    auto SourceBeforeEnd = Source(Current);
    FSavedMovePtr Saved(new FSavedMove_RPGRiding()); auto* Data = F.Move->GetPredictionData_Client_Character(); Data->SavedMoves.Add(Saved); Data->PendingMove = Saved;
    Saved->SavedRootMotion.PendingAddRootMotionSources.Add(SourceBeforeEnd); F.Pawn->SavedRootMotion = Saved->SavedRootMotion; F.Move->CurrentRootMotion = Saved->SavedRootMotion;
    F.State(Foot(Current));
    TestFalse(TEXT("Terminal RepNotify purges current/character/saved/pending snapshots"), F.Move->CurrentRootMotion.HasActiveRootMotionSources() || F.Pawn->SavedRootMotion.HasActiveRootMotionSources() || Saved->SavedRootMotion.HasActiveRootMotionSources());
    auto Fresh = Current; Fresh.Key = {FGuid::NewGuid(),1,1}; Fresh.ExecutionId = FGuid::NewGuid(); F.State(Fresh);
    F.Move->CurrentRootMotion.PendingAddRootMotionSources.Add(Source(Fresh));
    auto Older = Fresh; Older.Action = ERPGRidingAction::None; Older.Key.ActionSequence = 0; Older.ExecutionId.Invalidate(); Older.ActionDuration = 0; Older.ActionStartServerWorldSeconds = 0; Older.ActionDirection = FVector::ZeroVector;
    F.State(Older);
    TestTrue(TEXT("Rejected older RepNotify does not discard current Dash"), F.Riding->GetRidingState().ExecutionId == Fresh.ExecutionId && F.Move->CurrentRootMotion.HasActiveRootMotionSources());
    return true;
}
#endif
