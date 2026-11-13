#include "UI/Slots/SRPGSlotFrame.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

void SRPGSlotFrame::Construct(const FArguments& Args)
{
    Appearance = Args._Appearance;
    SetCanTick(true);
    ChildSlot[Args._Content.Widget];
}

void SRPGSlotFrame::ResetHover()
{
    if (HoverAlpha != 0.f)
    {
        HoverAlpha = 0.f;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

void SRPGSlotFrame::OnMouseLeave(const FPointerEvent& MouseEvent)
{
    SCompoundWidget::OnMouseLeave(MouseEvent);
    ResetHover();
}

void SRPGSlotFrame::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    const bool bHover = IsHovered() && IsEnabled() &&
        !(FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping());
    const FRPGSlotAppearance Current = Appearance.Get();
    const float NewAlpha = bHover ? FMath::FInterpConstantTo(HoverAlpha, 1.f, DeltaTime,
        1.f / FMath::Max(Current.FadeInDuration, 0.01f)) : 0.f;
    if (NewAlpha != HoverAlpha)
    {
        HoverAlpha = NewAlpha;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

int32 SRPGSlotFrame::OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
    const FSlateRect& CullingRect, FSlateWindowElementList& Elements, int32 Layer,
    const FWidgetStyle& Style, bool bParentEnabled) const
{
    const int32 ContentLayer = SCompoundWidget::OnPaint(Args, Geometry, CullingRect,
        Elements, Layer, Style, bParentEnabled);
    const FRPGSlotAppearance Current = Appearance.Get();
    const FVector2D Size = Geometry.GetLocalSize();
    const float Width = FMath::Min(FMath::Clamp(Current.HighlightWidth, 0.f, 16.f),
        static_cast<float>(FMath::Min(Size.X, Size.Y)) * 0.25f);
    if (!IsHovered() || HoverAlpha <= 0.f || Width <= 0.f || !bParentEnabled || !IsEnabled())
        return ContentLayer;

    // Transparent fill preserves the caller's icon, key, count and cooldown presentation.
    constexpr int32 RingCount = 10;
    for (int32 Ring = 0; Ring < RingCount; ++Ring)
    {
        const float Fraction = static_cast<float>(Ring) / (RingCount - 1);
        const float Inset = 0.5f + Fraction * Width;
        const FVector2D RingSize = Size - FVector2D(Inset * 2.f);
        if (RingSize.X <= 0.f || RingSize.Y <= 0.f) break;
        FLinearColor Color = Current.HighlightColor * Style.GetColorAndOpacityTint();
        Color.A *= HoverAlpha * FMath::Square(1.f - Fraction);
        const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent,
            FMath::Max(4.f - Inset, 0.f), Color, 1.f);
        FSlateDrawElement::MakeBox(Elements, ContentLayer + 1,
            Geometry.ToPaintGeometry(RingSize, FSlateLayoutTransform(FVector2D(Inset))),
            &Brush, ESlateDrawEffect::None, FLinearColor::Transparent);
    }
    return ContentLayer + 1;
}
