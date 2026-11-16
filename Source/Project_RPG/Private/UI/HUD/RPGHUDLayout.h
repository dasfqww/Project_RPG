#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"

/** Presentation-only reservations, in DPI-scaled local-player layout units.
 * Keep quest and practice cards in separate bounded, scrollable regions. */
namespace RPGHUDLayout
{
    inline constexpr float Edge = 24.f;
    inline constexpr float SideWidth = 300.f;
    inline constexpr float QuestTop = 76.f;
    inline constexpr float QuestHeight = 156.f;
    inline constexpr float PracticeTop = 248.f;
    inline constexpr float PracticeHeight = 264.f;
    inline constexpr float CombatHeight = 180.f;
    inline constexpr float ProgressBottom = 288.f;
    inline FLinearColor PanelColor() { return FLinearColor(.035f, .026f, .021f, .92f); }
    inline FLinearColor HeadingColor() { return FLinearColor(.92f, .77f, .55f); }
    inline FLinearColor BodyColor() { return FLinearColor(.85f, .81f, .73f); }

    inline bool IsWorldLabelInSafeArea(FVector2D Point, FVector2D Viewport)
    {
        constexpr float HalfLabelWidth = 130.f;
        const float Margin = Edge + SideWidth + HalfLabelWidth;
        return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y)
            && Point.X >= Margin && Point.X <= Viewport.X - Margin
            && Point.Y >= 100.f && Point.Y <= Viewport.Y - CombatHeight - 80.f;
    }

    inline void PlaceRightCard(USizeBox& Size, UCanvasPanelSlot& Slot, bool bPractice)
    {
        Size.SetWidthOverride(SideWidth);
        Size.SetMaxDesiredHeight(bPractice ? PracticeHeight : QuestHeight);
        Slot.SetAnchors(FAnchors(1.f, 0.f));
        Slot.SetAlignment(FVector2D(1.f, 0.f));
        Slot.SetPosition(FVector2D(-Edge, bPractice ? PracticeTop : QuestTop));
        Slot.SetAutoSize(true);
    }
}
