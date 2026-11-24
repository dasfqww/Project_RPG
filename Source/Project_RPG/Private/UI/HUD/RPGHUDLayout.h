#pragma once

#include "CoreMinimal.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "UI/Minimap/RPGPlayerMinimap.h"

/** Presentation-only reservations, in DPI-scaled local-player layout units.
 * Keep quest and practice cards in separate bounded, scrollable regions. */
namespace RPGHUDLayout
{
    inline constexpr float Edge = 24.f;
    inline constexpr float SideWidth = 300.f;
    inline constexpr float PartyTop = 76.f;
    inline constexpr float MinimapTop = 56.f;
    inline constexpr float MinimapSize = 280.f;
    inline constexpr float RightCardGap = 16.f;
    inline constexpr float QuestTop = MinimapTop + MinimapSize + RightCardGap;
    inline constexpr float QuestHeight = 156.f;
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

    inline constexpr float RightCardTop(bool bPractice, bool bHasMinimap)
    {
        if (!bPractice) return QuestTop;
        return bHasMinimap ? QuestTop + QuestHeight + RightCardGap : QuestTop - RightCardGap - PracticeHeight;
    }

    inline void PlaceRightCard(USizeBox& Size, UCanvasPanelSlot& Slot, bool bPractice)
    {
        Size.SetWidthOverride(SideWidth);
        Size.SetMaxDesiredHeight(bPractice ? PracticeHeight : QuestHeight);
        Slot.SetAnchors(FAnchors(1.f, 0.f));
        Slot.SetAlignment(FVector2D(1.f, 0.f));
        const bool bHasMinimap = bPractice && RPGPlayerMinimap::SupportsWorld(Size.GetWorld());
        Slot.SetPosition(FVector2D(-Edge, RightCardTop(bPractice, bHasMinimap)));
        Slot.SetAutoSize(true);
    }
}
