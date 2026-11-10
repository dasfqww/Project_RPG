#include "Riding/RPGRidingDashGeometry.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool RPGRidingDashGeometry::Usable(ACharacter* C, UCharacterMovementComponent* M)
{
    return IsValid(C) && !C->IsActorBeingDestroyed() && IsValid(M) && M->IsActive()
        && M->UpdatedComponent && M->UpdatedComponent == C->GetCapsuleComponent()
        && C->GetActorEnableCollision() && M->UpdatedComponent->IsQueryCollisionEnabled() && M->MovementMode == MOVE_Walking
        && M->GetGravityDirection().Equals(FVector(0,0,-1))
        && C->GetCapsuleComponent()->GetUpVector().Equals(FVector::UpVector, .001);
}
double RPGRidingDashGeometry::SafeFraction(ACharacter& C, UCharacterMovementComponent& M,
    const FVector& Start, const FVector& End, double Limit)
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
