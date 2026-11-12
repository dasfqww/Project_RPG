#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"

namespace RPGInventoryArt
{
// Optional collapsed templates keep art editable and textures referenced for
// cooking. Windows without these templates keep their existing appearance.
inline void ApplyBagButtonArt(UWidgetTree* Tree, UButton* Button)
{
	const auto* Template = Tree ? Cast<UButton>(Tree->FindWidget(TEXT("InventoryActionButtonStyle"))) : nullptr;
	if (!Template || !Button || Button == Template) return;
	Button->SetStyle(Template->GetStyle());
	Button->SetBackgroundColor(FLinearColor::White);
	if (auto* Slot = Cast<UHorizontalBoxSlot>(Button->Slot))
		Slot->SetPadding(FMargin(0.f, 4.f, 6.f, 4.f));
	if (auto* Text = Cast<UTextBlock>(Button->GetContent()))
	{
		Text->SetColorAndOpacity(FSlateColor::UseForeground());
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 11;
		Text->SetFont(Font);
	}
}

inline void ApplyBagSlotArt(UWidgetTree* Tree, UUserWidget* Cell)
{
	const auto* Template = Tree ? Cast<UImage>(Tree->FindWidget(TEXT("InventorySlotFrameStyle"))) : nullptr;
	if (!Template || !Template->GetBrush().GetResourceObject() || !Cell || !Cell->WidgetTree) return;
	if (Cell->WidgetTree->FindWidget(TEXT("InventorySlotArt"))) return;
	auto* Frame = Cast<UBorder>(Cell->GetRootWidget());
	auto* Layers = Frame ? Cast<UOverlay>(Frame->GetContent()) : nullptr;
	if (!Layers) return;
	// Decoration leaves the pending/recovery/drop-target tint underneath it and
	// preserves icons, counts, tooltips and mouse routing.
	auto* Art = Cell->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("InventorySlotArt"));
	Art->SetBrush(Template->GetBrush());
	Art->SetVisibility(ESlateVisibility::HitTestInvisible);
	auto* Slot = Layers->AddChildToOverlay(Art);
	Slot->SetHorizontalAlignment(HAlign_Fill);
	Slot->SetVerticalAlignment(VAlign_Fill);
	Slot->SetPadding(FMargin(-3.f));
}
}
