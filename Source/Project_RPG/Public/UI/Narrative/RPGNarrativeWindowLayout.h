#pragma once

#include "CoreMinimal.h"
#include "Layout/SlateRect.h"

class UWidgetTree;
class UCanvasPanel;
class USizeBox;
class UBorder;
class UVerticalBox;
class UScrollBox;
class UTextBlock;

/** The widget tree owns these controls. Header and footer stay outside the scrolling content. */
struct PROJECT_RPG_API FRPGNarrativeWindowFrame
{
    UCanvasPanel* Root = nullptr;
    USizeBox* Bounds = nullptr;
    UBorder* Border = nullptr;
    UVerticalBox* Header = nullptr;
    UVerticalBox* Content = nullptr;
    UVerticalBox* Footer = nullptr;
    UScrollBox* Scroll = nullptr;
};

struct PROJECT_RPG_API FRPGNarrativeWindowLayout
{
    static FRPGNarrativeWindowFrame Build(UWidgetTree& Tree);
    static FVector2D SizeForViewport(FVector2D LogicalViewport);
    static void ApplyViewport(USizeBox* Bounds, UBorder* Border, FVector2D LogicalViewport);
    /** Entry previews use one line with ellipsis; active choices retain their authored text and wrapping. */
    static void SetActionLabel(UTextBlock& Label, const FText& Caption, bool bEntryPreview);
    /** Geometric visibility within this scroll and viewport, not proof of reading or overlay occlusion. */
    static bool HasVisibleArea(const FSlateRect& Body, const FSlateRect& Scroll, const FSlateRect& Viewport);
};
