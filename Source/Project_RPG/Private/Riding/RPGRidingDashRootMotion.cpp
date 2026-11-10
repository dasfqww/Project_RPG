#include "Riding/RPGRidingDashRootMotion.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

FRootMotionSource_RPGRidingDash::FRootMotionSource_RPGRidingDash()
{
    Priority = 150;
    AccumulateMode = ERootMotionAccumulateMode::Override;
    Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
}
bool FRootMotionSource_RPGRidingDash::HasValidIdentity() const
{
    return RidingKey.RiderEpoch.IsValid() && RidingKey.StateRevision > 0 && RidingKey.ActionSequence > 0
        && RidingExecution.IsValid();
}
bool FRootMotionSource_RPGRidingDash::IsCurrent(const FRPGRidingRepState& State) const
{
    return HasValidIdentity() && RPGRiding::IsCanonical(State) && State.Mode == ERPGRidingMode::Mounted
        && State.Action == ERPGRidingAction::Dash && RidingExecution == State.ExecutionId
        && RPGRiding::Compare(RidingKey, State.Key) == ERPGRidingStateOrder::Same;
}
FRootMotionSource* FRootMotionSource_RPGRidingDash::Clone() const { return new FRootMotionSource_RPGRidingDash(*this); }
UScriptStruct* FRootMotionSource_RPGRidingDash::GetScriptStruct() const { return StaticStruct(); }
bool FRootMotionSource_RPGRidingDash::Matches(const FRootMotionSource* Other) const
{
    if (!Other || Other->GetScriptStruct() != StaticStruct()) return false;
    const auto& R = static_cast<const FRootMotionSource_RPGRidingDash&>(*Other);
    return HasValidIdentity() && R.HasValidIdentity() && RidingExecution == R.RidingExecution
        && RPGRiding::Compare(RidingKey, R.RidingKey) == ERPGRidingStateOrder::Same
        && FRootMotionSource_ConstantForce::Matches(Other);
}
bool FRootMotionSource_RPGRidingDash::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& Success)
{
    // This type has a constant, world-space force with no UObject curve. Avoid a curve/object payload.
    bool BaseSuccess = false;
    const bool Base = FRootMotionSource::NetSerialize(Ar, Map, BaseSuccess);
    Ar << Force << RidingKey.RiderEpoch << RidingKey.StateRevision << RidingKey.ActionSequence << RidingExecution;
    Success = Base && BaseSuccess && !Ar.IsError() && HasValidIdentity() && !Force.ContainsNaN()
        && FMath::IsFinite(Duration) && Duration > 0 && FMath::IsFinite(GetTime()) && GetTime() >= 0
        && AccumulateMode == ERootMotionAccumulateMode::Override && !bInLocalSpace && !StrengthOverTime;
    if (!Success) Ar.SetError();
    return Success;
}

bool RPGRidingDashMotion::RejectStale(FRootMotionSourceGroup& Group, const FRPGRidingRepState& State)
{
    const auto Invalid = [&State](const TSharedPtr<FRootMotionSource>& S)
    {
        return S.IsValid() && S->GetScriptStruct() == FRootMotionSource_RPGRidingDash::StaticStruct()
            && !static_cast<const FRootMotionSource_RPGRidingDash&>(*S).IsCurrent(State);
    };
    const int32 Removed = Group.RootMotionSources.RemoveAll(Invalid) + Group.PendingAddRootMotionSources.RemoveAll(Invalid);
    if (!Removed) return false;
    // Removal must not execute an old finish-velocity override or advance any foreign source's clock.
    Group.bHasOverrideSources = Group.bHasAdditiveSources = Group.bHasOverrideSourcesWithIgnoreZAccumulate = false;
    Group.LastAccumulatedSettings.Clear();
    for (const auto& S : Group.RootMotionSources)
    {
        if (!S.IsValid() || S->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
            || S->Status.HasFlag(ERootMotionSourceStatusFlags::Finished)) continue;
        const bool Override = S->AccumulateMode == ERootMotionAccumulateMode::Override;
        Group.bHasOverrideSources |= Override;
        Group.bHasAdditiveSources |= !Override;
        Group.bHasOverrideSourcesWithIgnoreZAccumulate |= Override && S->Settings.HasFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
        Group.LastAccumulatedSettings += S->Settings;
    }
    return true;
}
void RPGRidingDashMotion::Reconcile(ACharacter& C, const FRPGRidingRepState& State, bool IncludeSavedMoves)
{
    auto* M = C.GetCharacterMovement();
    if (!M) return;
    const bool RemovedCurrent = RejectStale(M->CurrentRootMotion, State);
    RejectStale(C.SavedRootMotion, State);
    if (IncludeSavedMoves && C.GetLocalRole() == ROLE_AutonomousProxy)
    {
        auto* Data = M->GetPredictionData_Client_Character();
        for (const auto& Move : Data->SavedMoves) if (Move.IsValid()) RejectStale(Move->SavedRootMotion, State);
        if (Data->PendingMove.IsValid()) RejectStale(Data->PendingMove->SavedRootMotion, State);
    }
    if (RemovedCurrent && M->IsMovingOnGround() && !M->CurrentRootMotion.HasActiveRootMotionSources()
        && !(C.GetMesh() && C.GetMesh()->IsPlayingRootMotion()))
    {
        const FVector XY = FVector(M->Velocity.X, M->Velocity.Y, 0).GetClampedToMaxSize(FMath::Max(0.f, M->GetMaxSpeed()));
        M->Velocity.X = XY.X; M->Velocity.Y = XY.Y; // Keep external vertical velocity and current walking policy.
        M->UpdateComponentVelocity();
    }
}
