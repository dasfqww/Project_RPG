#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

namespace RPGWindowDragConstraints
{
/** Clamp in screen pixels before converting movement to DPI-scaled layout units. */
inline FVector2D ClampScreenDelta(
	const FSlateRect& InitialTitleBounds,
	const FSlateRect& ScreenBounds,
	const FVector2D& DesiredDelta)
{
	if (!InitialTitleBounds.IsValid() || InitialTitleBounds.IsEmpty() ||
		!ScreenBounds.IsValid() || ScreenBounds.IsEmpty())
	{
		return DesiredDelta;
	}

	const double LeftAligned = ScreenBounds.Left - InitialTitleBounds.Left;
	const double RightAligned = ScreenBounds.Right - InitialTitleBounds.Right;
	const double TopAligned = ScreenBounds.Top - InitialTitleBounds.Top;
	const double BottomAligned = ScreenBounds.Bottom - InitialTitleBounds.Bottom;
	// A fitting title stays entirely visible. An oversized title may pan between
	// its two edge alignments, keeping the available screen covered by the title.
	return FVector2D(
		FMath::Clamp(DesiredDelta.X, FMath::Min(LeftAligned, RightAligned), FMath::Max(LeftAligned, RightAligned)),
		FMath::Clamp(DesiredDelta.Y, FMath::Min(TopAligned, BottomAligned), FMath::Max(TopAligned, BottomAligned)));
}
}
