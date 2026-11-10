#include "Riding/RPGRidingDashPrediction.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingDashGeometry.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Riding/RPGRidingMovementPrediction.h"
#include "Riding/RPGRidingStatePolicy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"

bool FRPGRidingDashPrediction::ContextCurrent() const
{
    const auto* C = Cast<ACharacter>(Owner.GetOwner());
    FRPGRidingEvaluationContext Guards; Owner.BuildGuards(Guards);
    return C && C->GetLocalRole() == ROLE_AutonomousProxy && C->IsLocallyControlled()
        && Guards.bAlive && !Guards.bActionBlocked && !Guards.bSlowActive && Owner.IsDashActivationCurrent(Activation);
}
bool FRPGRidingDashPrediction::ForeignMotion() const
{
    const auto* M = Movement.Get(); const auto* C = M ? M->GetCharacterOwner() : nullptr;
    if (!M || !C || (C->GetMesh() && C->GetMesh()->IsPlayingRootMotion())) return true;
    const auto Foreign = [this](const TSharedPtr<FRootMotionSource>& S)
    {
        if (!S || S->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval) || S->Status.HasFlag(ERootMotionSourceStatusFlags::Finished)) return false;
        return S->GetScriptStruct() != FRootMotionSource_RPGRidingDash::StaticStruct()
            || !static_cast<const FRootMotionSource_RPGRidingDash&>(*S).IsCurrent(Predicted);
    };
    return M->CurrentRootMotion.RootMotionSources.ContainsByPredicate(Foreign) || M->CurrentRootMotion.PendingAddRootMotionSources.ContainsByPredicate(Foreign);
}
bool FRPGRidingDashPrediction::Start(FRPGRidingDashActivation A, const FRPGRidingDashIntent& Intent)
{
    auto* C = Cast<ACharacter>(Owner.GetOwner()); auto* M = C ? C->GetCharacterMovement() : nullptr;
    if (IsPending() || !A.PredictionKey.IsLocalClientKey() || !Intent.IsValid() || Intent.GrantId != A.Grant.GrantId
        || !Owner.IsDashActivationCurrent(A) || !Owner.IsDashOwnerTimingReady()
        || Owner.State.Action != ERPGRidingAction::None || RPGRiding::Compare(Intent.Expected, Owner.State.Key) != ERPGRidingStateOrder::Same
        || !C || C->GetLocalRole() != ROLE_AutonomousProxy || !RPGRidingDashGeometry::Usable(C, M)) return false;
    const auto Execution = A.ExecutionFor(Intent.Expected);
    if (!Execution.IsValid()) return false;
    Movement = M;
    if (ForeignMotion() || RPGRidingDashGeometry::SafeFraction(*C, *M, M->UpdatedComponent->GetComponentLocation(),
        M->UpdatedComponent->GetComponentLocation() + Intent.Direction, 1.) <= .001) { Movement.Reset(); return false; }
    Activation = A; Expected = Intent.Expected; Predicted = Owner.State;
    if (!ContextCurrent()) { Activation = {}; Expected = {}; Predicted = {}; Movement.Reset(); return false; }
    ++Predicted.Key.ActionSequence; Predicted.Action = ERPGRidingAction::Dash; Predicted.ExecutionId = Execution;
    Predicted.ActionDuration = A.Grant.Duration; Predicted.ActionDirection = Intent.Direction;
    Predicted.ActionStartServerWorldSeconds = Owner.GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
    Predicted.LastEndReason = ERPGRidingEndReason::None;
    StartedAt = Owner.GetWorld()->GetTimeSeconds(); Travelled = 0; LastPosition = M->UpdatedComponent->GetComponentLocation();
    bAcknowledged = bSuppressRemainder = bBoundary = false;
    MotionName = FName(*FString::Printf(TEXT("RidingDash_%s"), *Execution.ToString(EGuidFormats::Digits)));
    auto Source = MakeShared<FRootMotionSource_RPGRidingDash>();
    Source->RidingKey = Predicted.Key; Source->RidingExecution = Execution; Source->InstanceName = MotionName;
    Source->Force = Intent.Direction * (A.Grant.Distance / A.Grant.Duration); Source->Duration = A.Grant.Duration;
    M->ApplyRootMotionSource(Source);
    if (!HasMotion()) { Stop(); return false; }
    return true;
}
FRPGRidingRepState FRPGRidingDashPrediction::View() const
{
    const auto State = Owner.State;
    if (IsPending() && ContextCurrent())
    {
        if (RPGRiding::Compare(State.Key, Expected) == ERPGRidingStateOrder::Same && State.Action == ERPGRidingAction::None) return Predicted;
        if (State.ExecutionId == Predicted.ExecutionId && RPGRiding::Compare(State.Key, Predicted.Key) == ERPGRidingStateOrder::Same) return State;
    }
    // A stopped prediction must not revive through a delayed authority source, even after a retry.
    if (State.Action == ERPGRidingAction::Dash && State.Key.RiderEpoch == RetiredKey.RiderEpoch
        && State.Key.StateRevision == RetiredKey.StateRevision && State.Key.ActionSequence <= RetiredKey.ActionSequence) return {};
    return State;
}
bool FRPGRidingDashPrediction::HasMotion() const
{
    auto* M = Movement.Get(); const auto Source = M && !MotionName.IsNone() ? M->GetRootMotionSource(MotionName) : nullptr;
    return IsPending() && Source && Source->GetScriptStruct() == FRootMotionSource_RPGRidingDash::StaticStruct()
        && static_cast<const FRootMotionSource_RPGRidingDash&>(*Source).IsCurrent(Predicted)
        && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval) && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished)
        && Source->GetTime() < Source->Duration;
}
void FRPGRidingDashPrediction::ClampVelocity()
{
    auto* M = Movement.Get();
    if (!M || ForeignMotion()) return;
    const auto XY = bBoundary ? FVector::ZeroVector : FVector(M->Velocity.X, M->Velocity.Y, 0).GetClampedToMaxSize(FMath::Max(0.f, M->GetMaxSpeed()));
    M->Velocity.X = XY.X; M->Velocity.Y = XY.Y; M->UpdateComponentVelocity();
}
void FRPGRidingDashPrediction::Stop()
{
    if (!IsPending()) return;
    if (RetiredKey.RiderEpoch != Predicted.Key.RiderEpoch || RetiredKey.StateRevision != Predicted.Key.StateRevision
        || RetiredKey.ActionSequence < Predicted.Key.ActionSequence) RetiredKey = Predicted.Key;
    Activation = {}; Expected = {}; Predicted = {}; MotionName = NAME_None; Travelled = 0; bAcknowledged = false;
    if (auto* C = Cast<ACharacter>(Owner.GetOwner())) RPGRidingDashMotion::Reconcile(*C, View(), true);
    ClampVelocity();
}
void FRPGRidingDashPrediction::Update()
{
    if (!IsPending()) return;
    auto* M = Movement.Get(); auto* C = M ? M->GetCharacterOwner() : nullptr;
    if (!ContextCurrent() || !RPGRidingDashGeometry::Usable(C, M) || ForeignMotion()) { Stop(); return; }
    const auto S = Owner.State;
    const bool Accepted = S.ExecutionId == Predicted.ExecutionId && RPGRiding::Compare(S.Key, Predicted.Key) == ERPGRidingStateOrder::Same
        && S.Action == ERPGRidingAction::Dash && S.ActionDuration == Activation.Grant.Duration && S.ActionDirection.Equals(Predicted.ActionDirection, .001);
    if (Accepted) bAcknowledged = true;
    else if (bAcknowledged || S.Action != ERPGRidingAction::None || RPGRiding::Compare(S.Key, Expected) != ERPGRidingStateOrder::Same) { Stop(); return; }
    if ((!bAcknowledged && Owner.GetWorld()->GetTimeSeconds() - StartedAt >= 2.) || (bAcknowledged && !HasMotion())) { Stop(); return; }
    if (!HasMotion()) ClampVelocity(); // Keep the bounded receipt wait, never extend the local force.
}
void FRPGRidingDashPrediction::Prepare()
{
    bSuppressRemainder = bBoundary = false;
    if (auto* C = Cast<ACharacter>(Owner.GetOwner()); C && C->GetLocalRole() == ROLE_AutonomousProxy)
        RPGRidingDashMotion::Reconcile(*C, Owner.GetDashMovementState());
    Update();
}
void FRPGRidingDashPrediction::Finalize() { Update(); if (bSuppressRemainder) ClampVelocity(); }
float FRPGRidingDashPrediction::CooldownRemaining() const
{
    return IsPending() && Owner.GetWorld() ? FMath::Max(0., Activation.Grant.Cooldown - (Owner.GetWorld()->GetTimeSeconds() - StartedAt)) : 0;
}
double FRPGRidingDashPrediction::Constrain(const FVector& Start, const FVector& End)
{
    if (bSuppressRemainder) return 0;
    if (!IsPending()) return 1;
    auto* M = Movement.Get(); auto* C = M ? M->GetCharacterOwner() : nullptr;
    const auto Source = M && !MotionName.IsNone() ? M->GetRootMotionSource(MotionName) : nullptr;
    // PrepareRootMotion can finish the source before its final force delta is moved.
    // That last delta still needs the same floor/path budget; normal walking after removal does not.
    if (!Source || Source->GetScriptStruct() != FRootMotionSource_RPGRidingDash::StaticStruct()
        || !static_cast<const FRootMotionSource_RPGRidingDash&>(*Source).IsCurrent(Predicted)
        || Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)) return 1;
    if (!ContextCurrent() || !RPGRidingDashGeometry::Usable(C, M) || Start.ContainsNaN() || End.ContainsNaN() || !Start.Equals(LastPosition, .1)) { Stop(); return 0; }
    const double Length = (End - Start).Size();
    if (Length <= UE_SMALL_NUMBER) return 1;
    const double Fraction = RPGRidingDashGeometry::SafeFraction(*C, *M, Start, End, FMath::Min(1., FMath::Max(0., Activation.Grant.Distance - Travelled) / Length));
    if (Fraction < 1.) { bBoundary = bSuppressRemainder = true; Stop(); }
    return Fraction;
}
void FRPGRidingDashPrediction::Record(FGuid Execution, const FVector& Start, const FVector& End)
{
    if (!IsPending() || Predicted.ExecutionId != Execution) return;
    if (Start.ContainsNaN() || End.ContainsNaN()) { Stop(); return; }
    Travelled += (End - Start).Size(); LastPosition = End;
    if (Travelled >= Activation.Grant.Distance - .001) { bSuppressRemainder = true; Stop(); }
}
void FRPGRidingDashPrediction::Restore(const FRPGRidingMoveStamp& Stamp, double Distance)
{
    if (!IsPending() || Stamp.ExecutionId != Predicted.ExecutionId || RPGRiding::Compare(Stamp.Key, Predicted.Key) != ERPGRidingStateOrder::Same
        || !FMath::IsFinite(Distance) || Distance < 0 || Distance > Activation.Grant.Distance) return;
    if (auto* M = Movement.Get(); M && M->UpdatedComponent) { Travelled = Distance; LastPosition = M->UpdatedComponent->GetComponentLocation(); }
}
