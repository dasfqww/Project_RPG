#pragma once

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

// Presentation only: keep named controls and their dispatch/authority guards in
// the widget. Wrappers must not replace a button's direct text child (retry UI).
namespace RPGOnlineLobbyLayout
{
	inline UTextBlock* Text(UWidgetTree& Tree, UVerticalBox& Parent, FName Name,
		const FText& Label, int32 FontSize = 14)
	{
		auto* Result = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Result->SetAutoWrapText(true);
		Result->SetText(Label);
		auto Font = Result->GetFont(); // Retain the existing font/fallback assets.
		Font.Size = FontSize;
		Result->SetFont(Font);
		Result->SetColorAndOpacity(FSlateColor(FLinearColor(.9f, .93f, .97f)));
		Parent.AddChildToVerticalBox(Result)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		return Result;
	}

	inline void Control(UWidgetTree& Tree, UVerticalBox& Parent, UWidget& Child)
	{
		auto* Size = Tree.ConstructWidget<USizeBox>();
		Size->SetMinDesiredHeight(44.f); // Minimum, not fixed: wrapped text may grow.
		Size->SetContent(&Child);
		Parent.AddChildToVerticalBox(Size)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	inline void SetControlVisibility(UWidget& Child, ESlateVisibility Visibility)
	{
		Child.SetVisibility(Visibility);
		// A collapsed child must not leave its minimum-height wrapper behind.
		if (auto* Size = Cast<USizeBox>(Child.GetParent()); Size && Size->GetContent() == &Child)
			Size->SetVisibility(Visibility);
	}

	inline UVerticalBox* Section(UWidgetTree& Tree, UVerticalBox& Parent, FName Name,
		const FText& Title)
	{
		auto* Card = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		Card->SetBrushColor(FLinearColor(.055f, .075f, .105f, 1.f));
		Card->SetPadding(FMargin(16.f));
		Parent.AddChildToVerticalBox(Card)->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		auto* Rows = Tree.ConstructWidget<UVerticalBox>();
		Card->SetContent(Rows);
		Text(Tree, *Rows, FName(*(Name.ToString() + TEXT("Title"))), Title, 18);
		return Rows;
	}

	inline UEditableTextBox* Input(UWidgetTree& Tree, UVerticalBox& Parent, FName Name, const FText& Hint)
	{
		auto* Result = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
		Result->SetHintText(Hint);
		Control(Tree, Parent, *Result);
		return Result;
	}

	inline UButton* Action(UWidgetTree& Tree, UVerticalBox& Parent, FName Name, const FText& Label)
	{
		auto* Result = Tree.ConstructWidget<UButton>(UButton::StaticClass(), Name);
		auto* Caption = Tree.ConstructWidget<UTextBlock>();
		Caption->SetAutoWrapText(true);
		Caption->SetText(Label);
		auto Font = Caption->GetFont();
		Font.Size = 14;
		Caption->SetFont(Font);
		Caption->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Caption->SetJustification(ETextJustify::Center);
		Result->SetContent(Caption);
		if (auto* ContentSlot = Cast<UButtonSlot>(Caption->Slot))
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		auto Style = Result->GetStyle();
		Style.NormalPadding = FMargin(12.f, 8.f);
		Style.PressedPadding = FMargin(12.f, 9.f, 12.f, 7.f);
		Result->SetStyle(Style);
		Control(Tree, Parent, *Result);
		return Result;
	}

	inline UTextBlock* UnavailableReason(UWidgetTree& Tree, UVerticalBox& Parent, FName ActionName)
	{
		auto* Result = Text(Tree, Parent, FName(*(ActionName.ToString() + TEXT("UnavailableReason"))), FText::GetEmpty(), 12);
		Result->SetColorAndOpacity(FSlateColor(FLinearColor(.76f, .8f, .86f)));
		Result->SetVisibility(ESlateVisibility::Collapsed);
		return Result;
	}
}
