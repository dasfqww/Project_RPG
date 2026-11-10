#include "Riding/RPGRidingDashRuntime.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "HAL/PlatformTime.h"

namespace
{
    bool ForeignMotion(const UCharacterMovementComponent& M, FName Owned = NAME_None)
    {
        const auto Foreign = [Owned](const TSharedPtr<FRootMotionSource>& S)
        { return S.IsValid() && (Owned.IsNone() || S->InstanceName != Owned)
            && !S->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
            && !S->Status.HasFlag(ERootMotionSourceStatusFlags::Finished); };
        auto* C = M.GetCharacterOwner();
        return (C && C->GetMesh() && C->GetMesh()->IsPlayingRootMotion())
            || M.CurrentRootMotion.RootMotionSources.ContainsByPredicate(Foreign)
            || M.CurrentRootMotion.PendingAddRootMotionSources.ContainsByPredicate(Foreign);
    }
    bool Usable(ACharacter* C, UCharacterMovementComponent* M)
    {
        return IsValid(C) && !C->IsActorBeingDestroyed() && C->HasAuthority() && IsValid(M) && M->IsActive()
            && M->UpdatedComponent && M->UpdatedComponent == C->GetCapsuleComponent()
            && C->GetActorEnableCollision() && M->UpdatedComponent->IsQueryCollisionEnabled() && M->MovementMode == MOVE_Walking
            && M->GetGravityDirection().Equals(FVector(0,0,-1))
            && C->GetCapsuleComponent()->GetUpVector().Equals(FVector::UpVector, .001);
    }
}

double FRPGRidingDashRuntime::SafeFraction(ACharacter& C, UCharacterMovementComponent& M,
    const FVector& Start, const FVector& End, double Limit) const
{
    const FVector Delta = End - Start;
    const double Length = Delta.Size();
    if (Length <= UE_SMALL_NUMBER) return 1;
    auto* Capsule = C.GetCapsuleComponent();
    const double Step = FMath::Clamp(double(Capsule->GetScaledCapsuleRadius()) * .5, 5., 25.);
    Limit = FMath::Min(Limit, 256. * Step / Length);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RidingDash), false, &C);
    FHitResult Hit;
    const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight());
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    if (C.GetWorld()->SweepSingleByChannel(Hit, Start, Start + Delta * Limit, Capsule->GetComponentQuat(),
        Capsule->GetCollisionObjectType(), Shape, Query, Responses)) Limit *= Hit.bStartPenetrating ? 0. : Hit.Time;
    const int32 Count = FMath::Max(1, FMath::CeilToInt(Length * Limit / Step));
    double Safe = 0;
    for (int32 I = 1; I <= Count; ++I)
    {
        const double Fraction = Limit * I / Count;
        FFindFloorResult Floor; M.FindFloor(Start + Delta * Fraction, Floor, false);
        if (!Floor.IsWalkableFloor() || Floor.FloorDist > M.MaxStepHeight + 2.f) return Safe;
        Safe = Fraction;
    }
    return Limit;
}

bool FRPGRidingDashRuntime::CanStart(const FRPGRidingDashProfile& P, const FVector& Direction) const
{
    auto* C = Cast<ACharacter>(Owner.GetOwner()); auto* M = C ? C->GetCharacterMovement() : nullptr;
    auto* H = M ? Cast<IRPGRidingDashMotionHost>(M) : nullptr;
    if (bStopping || Active.Ticket.ExecutionId.IsValid() || !Usable(C, M) || !H || !H->CanOpenRidingDashWindow() || ForeignMotion(*M)) return false;
    if (!FMath::IsFinite(P.Distance) || !FMath::IsFinite(P.Duration) || P.Distance <= 0 || P.Duration <= 0) return false;
    const FVector Start = M->UpdatedComponent->GetComponentLocation();
    return SafeFraction(*C, *M, Start, Start + Direction * FMath::Min(P.Distance, 1.f), 1.) > .001;
}

ERPGRidingResultCode FRPGRidingDashRuntime::Start(FRPGRidingStateKey Expected, FVector Direction)
{
    if (!IsInGameThread()) return ERPGRidingResultCode::AuthorityRevoked;
    if (bStarting || bStopping || Active.Ticket.ExecutionId.IsValid()) return ERPGRidingResultCode::Busy;
    TGuardValue<bool> Starting(bStarting, true);
    FRPGRidingDashTicket T;
    const auto Result = Owner.TryBeginDash(Expected, Direction, [this](const auto& P, const auto& D) { return CanStart(P, D); }, T);
    if (Result != ERPGRidingResultCode::Applied) return Result;
    if (!Owner.IsDashCurrent(T)) return Result; // Accepted then cancelled: retain cooldown; allocate nothing.
    auto* C = Cast<ACharacter>(Owner.GetOwner()); auto* M = C ? C->GetCharacterMovement() : nullptr;
    auto* H = M ? Cast<IRPGRidingDashMotionHost>(M) : nullptr;
    if (!Usable(C, M) || !H || !CanStart(T.Profile, Direction)) { Owner.FinishDash(T, ERPGRidingEndReason::ExecutionRejected); return Result; }
    Active.Ticket = T; Active.Movement = M; Active.LastPosition = M->UpdatedComponent->GetComponentLocation();
    const auto Window = H->OpenRidingDashWindow(T.Profile.Duration + .2f, T.Profile.Distance + 25.f);
    if (Active.Ticket.ExecutionId != T.ExecutionId || !Active.Movement.IsValid() || !Owner.IsDashCurrent(T)
        || !Window.Owner.IsValid() || !Window.Handle.IsValid() || !Window.Close)
    {
        Window.Release();
        Stop(ERPGRidingEndReason::ExecutionRejected);
        return Result;
    }
    Active.Window = Window;
    if (ForeignMotion(*M) || !Usable(C, M)) { Stop(ERPGRidingEndReason::ExecutionRejected); return Result; }
    Active.MotionName = FName(*FString::Printf(TEXT("RidingDash_%s"), *T.ExecutionId.ToString(EGuidFormats::Digits)));
    auto Source = MakeShared<FRootMotionSource_RPGRidingDash>();
    Source->RidingKey = T.Key; Source->RidingExecution = T.ExecutionId;
    Source->InstanceName = Active.MotionName; Source->Priority = 150;
    Source->AccumulateMode = ERootMotionAccumulateMode::Override;
    Source->Force = Direction * (T.Profile.Distance / T.Profile.Duration); Source->Duration = T.Profile.Duration;
    Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
    Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
    M->ApplyRootMotionSource(Source);
    if (!HasMotion()) Stop(ERPGRidingEndReason::ExecutionRejected);
    return Result;
}

bool FRPGRidingDashRuntime::HasMotion() const
{
    auto* M = Active.Movement.Get();
    if (!M || Active.MotionName.IsNone() || !Owner.MatchesDash(Active.Ticket)) return false;
    const auto Source = M->GetRootMotionSource(Active.MotionName);
    return Source.IsValid() && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
        && !Source->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
}

void FRPGRidingDashRuntime::PrepareMovement() { bSuppressRemainder = false; bStopAtBoundary = false; Tick(); }
void FRPGRidingDashRuntime::Tick()
{
    if (!Active.Ticket.ExecutionId.IsValid() || bStarting || bStopping) return;
    auto* M = Active.Movement.Get(); auto* C = M ? M->GetCharacterOwner() : nullptr;
    if (!Owner.MatchesDash(Active.Ticket) || !Usable(C, M) || ForeignMotion(*M, Active.MotionName)) Stop(ERPGRidingEndReason::ExecutionRejected);
    else if (FPlatformTime::Seconds() >= Owner.DashDeadline || !HasMotion()) Stop(ERPGRidingEndReason::Completed);
}

double FRPGRidingDashRuntime::Constrain(const FVector& Start, const FVector& End)
{
    if (bSuppressRemainder) return 0;
    if (!Active.Ticket.ExecutionId.IsValid()) return 1;
    auto* M = Active.Movement.Get(); auto* C = M ? M->GetCharacterOwner() : nullptr;
    if (!Usable(C, M) || !Owner.MatchesDash(Active.Ticket) || Start.ContainsNaN() || End.ContainsNaN()
        || !Start.Equals(Active.LastPosition, .1)) { Stop(ERPGRidingEndReason::ExecutionRejected); return 0; }
    const double Length = (End - Start).Size();
    if (Length <= UE_SMALL_NUMBER) return 1;
    const double Remaining = FMath::Max(0., double(Active.Ticket.Profile.Distance) - Active.Travelled);
    const double Fraction = SafeFraction(*C, *M, Start, End, FMath::Min(1., Remaining / Length));
    if (Fraction < 1.)
    {
        const auto Key = Active.Ticket.Key;
        bStopAtBoundary = true;
        if (!ForeignMotion(*M, Active.MotionName)) { M->Velocity.X = 0; M->Velocity.Y = 0; }
        Stop(ERPGRidingEndReason::Completed);
        const auto S = Owner.GetRidingState();
        if (S.Mode != ERPGRidingMode::Mounted || S.Key.RiderEpoch != Key.RiderEpoch || S.Key.StateRevision != Key.StateRevision
            || S.Action != ERPGRidingAction::None || S.Key.ActionSequence != Key.ActionSequence + 1 || ForeignMotion(*M)) return 0;
    }
    return Fraction;
}

void FRPGRidingDashRuntime::RecordStep(const FVector& Start, const FVector& End)
{
    if (!Active.Ticket.ExecutionId.IsValid()) return;
    if (Start.ContainsNaN() || End.ContainsNaN()) { Stop(ERPGRidingEndReason::ExecutionRejected); return; }
    Active.Travelled += (End - Start).Size(); Active.LastPosition = End;
    if (Active.Travelled >= Active.Ticket.Profile.Distance - .001) Stop(ERPGRidingEndReason::Completed);
}

void FRPGRidingDashRuntime::Stop(ERPGRidingEndReason Reason, bool Finish)
{
    if (bStopping) return;
    TGuardValue<bool> Stopping(bStopping, true);
    const auto Old = Active; Active = {}; // Detach BEFORE callbacks can author another resource.
    auto* M = Old.Movement.Get(); auto* C = M ? M->GetCharacterOwner() : nullptr;
    if (M && !Old.MotionName.IsNone())
    {
        const bool HasForeign = ForeignMotion(*M, Old.MotionName);
        M->RemoveRootMotionSource(Old.MotionName);
        if (C) C->SavedRootMotion.RemoveRootMotionSource(Old.MotionName);
        bSuppressRemainder = true;
        StoppedMovement = M;
        if (!HasForeign)
        {
            const double Limit = Owner.GetRidingState().Movement.IsValid() ? Owner.GetRidingState().Movement.Speed : M->GetMaxSpeed();
            const auto XY = FVector(M->Velocity.X, M->Velocity.Y, 0).GetClampedToMaxSize(FMath::Max(0., Limit));
            M->Velocity.X = XY.X; M->Velocity.Y = XY.Y; // Preserve gravity/external vertical velocity.
        }
    }
    Old.Window.Release();
    if (Finish && Old.Ticket.ExecutionId.IsValid()) Owner.FinishDash(Old.Ticket, Reason);
}

void FRPGRidingDashRuntime::ClampAfterStateChange()
{
    auto* M = StoppedMovement.Get();
    if (!bSuppressRemainder || !M || M->GetOwner() != Owner.GetOwner() || ForeignMotion(*M)) return;
    const auto XY = bStopAtBoundary ? FVector::ZeroVector
        : FVector(M->Velocity.X, M->Velocity.Y, 0).GetClampedToMaxSize(FMath::Max(0.f, M->GetMaxSpeed()));
    M->Velocity.X = XY.X; M->Velocity.Y = XY.Y;
    M->UpdateComponentVelocity();
}
