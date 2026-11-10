#pragma once
#include "CoreMinimal.h"
class ACharacter;
class UCharacterMovementComponent;
namespace RPGRidingDashGeometry
{
    bool Usable(ACharacter* Character, UCharacterMovementComponent* Movement);
    double SafeFraction(ACharacter& Character, UCharacterMovementComponent& Movement,
        const FVector& Start, const FVector& End, double Limit);
}
