#pragma once
#include "CoreMinimal.h"

class UWidgetTree;
class UCanvasPanel;
class USizeBox;
class UBorder;
class UScrollBox;
class UVerticalBox;
class UTextBlock;

struct PROJECT_RPG_API FRPGTradingWindowFrame
{
    UCanvasPanel* Root=nullptr;
    USizeBox* Bounds=nullptr;
    UBorder* Border=nullptr;
    UScrollBox* Scroll=nullptr;
    UVerticalBox* Header=nullptr;
    UVerticalBox* Content=nullptr;
    UVerticalBox* Footer=nullptr;
};

/** Shared presentation frame only; layout never submits, cancels or restores a trade. */
struct PROJECT_RPG_API FRPGTradingWindowLayout
{
    static FRPGTradingWindowFrame Build(UWidgetTree& Tree,const TCHAR* Prefix,FVector2D Maximum);
    static FVector2D SizeForViewport(FVector2D LogicalViewport,FVector2D Maximum);
    static void Apply(USizeBox* Bounds,UBorder* Border,FVector2D LogicalViewport,FVector2D Maximum);
    static void FormatText(UTextBlock& Label,bool bTitle=false);
};
