#include "UI/Narrative/RPGNarrativeWindowLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

FVector2D FRPGNarrativeWindowLayout::SizeForViewport(const FVector2D Viewport)
{
    if (!FMath::IsFinite(Viewport.X) || !FMath::IsFinite(Viewport.Y) || Viewport.X <= 0 || Viewport.Y <= 0)
        return FVector2D(720, 620);
    const auto Inset = [](double Size) { return FMath::Min(24.0, Size * .05); };
    return FVector2D(FMath::Min(720.0, Viewport.X - 2 * Inset(Viewport.X)),
        FMath::Min(620.0, Viewport.Y - 2 * Inset(Viewport.Y)));
}
void FRPGNarrativeWindowLayout::ApplyViewport(USizeBox* Bounds, UBorder* Border, const FVector2D Viewport)
{
    if (!Bounds || !Border) return;
    const auto Size = SizeForViewport(Viewport);
    const float Width = static_cast<float>(Size.X), Height = static_cast<float>(Size.Y);
    if (!FMath::IsNearlyEqual(Bounds->GetWidthOverride(), Width)) Bounds->SetWidthOverride(Width);
    if (!FMath::IsNearlyEqual(Bounds->GetHeightOverride(), Height)) Bounds->SetHeightOverride(Height);
    const float Padding = FMath::Min(24.f, Height * .05f);
    if (!FMath::IsNearlyEqual(Border->GetPadding().Left, Padding)) Border->SetPadding(FMargin(Padding));
}
FRPGNarrativeWindowFrame FRPGNarrativeWindowLayout::Build(UWidgetTree& Tree)
{
    FRPGNarrativeWindowFrame F;
    F.Root = Tree.ConstructWidget<UCanvasPanel>(); Tree.RootWidget = F.Root;
    F.Bounds = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("NarrativeWindowBounds"));
    auto* Slot = F.Root->AddChildToCanvas(F.Bounds);
    Slot->SetAnchors(FAnchors(.5f)); Slot->SetAlignment(FVector2D(.5f)); Slot->SetAutoSize(true);
    F.Border = Tree.ConstructWidget<UBorder>(); F.Bounds->SetContent(F.Border);
    F.Border->SetBrushColor(FLinearColor(.025f, .035f, .055f, .98f));
    F.Border->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    auto* Shell = Tree.ConstructWidget<UVerticalBox>(); F.Border->SetContent(Shell);
    F.Header = Tree.ConstructWidget<UVerticalBox>(); Shell->AddChildToVerticalBox(F.Header);
    F.Scroll = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("NarrativeContentScroll"));
    F.Scroll->SetClipping(EWidgetClipping::ClipToBoundsAlways);
    F.Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);
    F.Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);
    Shell->AddChildToVerticalBox(F.Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    F.Content = Tree.ConstructWidget<UVerticalBox>(); F.Scroll->AddChild(F.Content);
    F.Footer = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("NarrativeFooter"));
    Shell->AddChildToVerticalBox(F.Footer);
    ApplyViewport(F.Bounds, F.Border, FVector2D::ZeroVector);
    return F;
}
bool FRPGNarrativeWindowLayout::HasVisibleArea(const FSlateRect& Body, const FSlateRect& Scroll, const FSlateRect& Viewport)
{
    for (const auto& R : {Body, Scroll, Viewport})
        if (!FMath::IsFinite(R.Left) || !FMath::IsFinite(R.Top) || !FMath::IsFinite(R.Right) || !FMath::IsFinite(R.Bottom)
            || R.Left >= R.Right || R.Top >= R.Bottom) return false;
    const auto Left = FMath::Max3(Body.Left, Scroll.Left, Viewport.Left);
    const auto Top = FMath::Max3(Body.Top, Scroll.Top, Viewport.Top);
    return Left < FMath::Min3(Body.Right, Scroll.Right, Viewport.Right)
        && Top < FMath::Min3(Body.Bottom, Scroll.Bottom, Viewport.Bottom);
}
