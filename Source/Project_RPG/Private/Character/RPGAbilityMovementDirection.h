#pragma once

#include "CoreMinimal.h"

namespace RPGAbilityMovement
{
	inline FVector ResolveDirection(const FVector& LocalInput, const FVector& ServerAcceleration, bool bUseServerAcceleration)
	{
		const FVector& Source = bUseServerAcceleration ? ServerAcceleration : LocalInput;
		if (Source.ContainsNaN()) { return FVector::ZeroVector; }
		const FVector Direction = Source.GetSafeNormal2D();
		return Direction.ContainsNaN() ? FVector::ZeroVector : Direction;
	}
}
