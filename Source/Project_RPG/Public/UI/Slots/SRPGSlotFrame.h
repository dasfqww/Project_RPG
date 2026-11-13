#pragma once

#include "UI/Slots/RPGSlotAppearance.h"
#include "Widgets/SCompoundWidget.h"

/** Shared slot chrome for authored UMG and native action cells. Never handles action input. */
class PROJECT_RPG_API SRPGSlotFrame : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRPGSlotFrame) : _Appearance(FRPGSlotAppearance()) {}
        SLATE_ATTRIBUTE(FRPGSlotAppearance, Appearance)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
        const FWidgetStyle& Style, bool bParentEnabled) const override;
    void ResetHover();

private:
    TAttribute<FRPGSlotAppearance> Appearance;
    float HoverAlpha = 0.f;
};
