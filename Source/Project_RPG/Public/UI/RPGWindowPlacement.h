#pragma once

#include "CoreMinimal.h"

class UUserWidget;

enum class ERPGWindowSide : uint8
{
	Left,
	Right,
	Center
};

/** A non-stretched, desired-size viewport placement in logical UI units. */
struct FRPGWindowPlacement
{
	FVector2D Anchor;
	FVector2D Alignment;
	FVector2D Offset;
};

/** Presentation only: no input locks, focus changes, or skill dependencies. */
namespace RPGWindowPlacement
{
PROJECT_RPG_API FRPGWindowPlacement Calculate(
	const FVector2D& ViewportSize, const FVector2D& DesiredSize,
	ERPGWindowSide Side, double EdgeInset = 24.0);

/** Call after activation so authored activation graphs cannot recenter the window.
 * Keeps natural Desired size, viewport Z-order, and the user's drag translation.
 * This is an opening-time baseline, not a per-frame layout/drag controller.
 */
PROJECT_RPG_API void Apply(UUserWidget& Window, ERPGWindowSide Side);
}
