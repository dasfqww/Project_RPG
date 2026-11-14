#include "UI/Lobby/RPGTradingWindowLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

void FRPGTradingWindowLayout::FormatText(UTextBlock& Label,bool bTitle)
{
    auto Font=Label.GetFont();Font.Size=bTitle?16:14;Label.SetFont(Font);
    Label.SetAutoWrapText(!bTitle);Label.SetTextOverflowPolicy(bTitle?ETextOverflowPolicy::Ellipsis:ETextOverflowPolicy::Clip);
    if(bTitle)Label.SetToolTipText(Label.GetText());
}
FVector2D FRPGTradingWindowLayout::SizeForViewport(FVector2D Viewport,FVector2D Maximum)
{
    if(!FMath::IsFinite(Viewport.X) || !FMath::IsFinite(Viewport.Y) || Viewport.X<=0 || Viewport.Y<=0)return Maximum;
    const auto Inset=[](double Value){return FMath::Min(16.0,Value*.05);};
    return FVector2D(FMath::Min(Maximum.X,Viewport.X-2*Inset(Viewport.X)),FMath::Min(Maximum.Y,Viewport.Y-2*Inset(Viewport.Y)));
}
void FRPGTradingWindowLayout::Apply(USizeBox* Bounds,UBorder* Border,FVector2D Viewport,FVector2D Maximum)
{
    if(!Bounds || !Border)return;
    const auto Size=SizeForViewport(Viewport,Maximum);
    if(!FMath::IsNearlyEqual(Bounds->GetWidthOverride(),static_cast<float>(Size.X)))Bounds->SetWidthOverride(static_cast<float>(Size.X));
    if(!FMath::IsNearlyEqual(Bounds->GetHeightOverride(),static_cast<float>(Size.Y)))Bounds->SetHeightOverride(static_cast<float>(Size.Y));
    const float Padding=static_cast<float>(FMath::Min(24.0,FMath::Min(Size.X,Size.Y)*.05));
    if(!FMath::IsNearlyEqual(Border->GetPadding().Left,Padding))Border->SetPadding(FMargin(Padding));
}
FRPGTradingWindowFrame FRPGTradingWindowLayout::Build(UWidgetTree& Tree,const TCHAR* Prefix,FVector2D Maximum)
{
    FRPGTradingWindowFrame F;
    F.Root=Tree.ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(FString(Prefix)+TEXT("Root")));Tree.RootWidget=F.Root;
    auto* Shade=Tree.ConstructWidget<UBorder>();Shade->SetBrushColor(FLinearColor(0,0,0,.75f));
    auto* ShadeSlot=F.Root->AddChildToCanvas(Shade);ShadeSlot->SetAnchors(FAnchors(0,0,1,1));ShadeSlot->SetOffsets(FMargin(0));
    F.Bounds=Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(FString(Prefix)+TEXT("WindowBounds")));
    auto* Slot=F.Root->AddChildToCanvas(F.Bounds);Slot->SetAnchors(FAnchors(.5f));Slot->SetAlignment(FVector2D(.5f));Slot->SetAutoSize(true);
    F.Border=Tree.ConstructWidget<UBorder>();F.Border->SetBrushColor(FLinearColor(.035f,.05f,.07f,1));F.Border->SetClipping(EWidgetClipping::ClipToBoundsAlways);F.Bounds->AddChild(F.Border);
    auto* Shell=Tree.ConstructWidget<UVerticalBox>();F.Border->SetContent(Shell);
    F.Header=Tree.ConstructWidget<UVerticalBox>();Shell->AddChildToVerticalBox(F.Header);
    F.Scroll=Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),FName(FString(Prefix)+TEXT("ContentScroll")));
    F.Scroll->SetClipping(EWidgetClipping::ClipToBoundsAlways);F.Scroll->SetScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll);F.Scroll->SetNavigationDestination(EDescendantScrollDestination::IntoView);
    Shell->AddChildToVerticalBox(F.Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    F.Content=Tree.ConstructWidget<UVerticalBox>();F.Scroll->AddChild(F.Content);
    F.Footer=Tree.ConstructWidget<UVerticalBox>();Shell->AddChildToVerticalBox(F.Footer);
    Apply(F.Bounds,F.Border,FVector2D::ZeroVector,Maximum);return F;
}
