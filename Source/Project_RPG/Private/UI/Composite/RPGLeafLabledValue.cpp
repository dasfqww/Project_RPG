// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGLeafLabledValue.h"
#include "Components/TextBlock.h"

void URPGLeafLabledValue::SetLablelValueText(UTextBlock* TextBlock, const FText& Text, bool bCollapse) const
{
	if (bCollapse)
	{
		TextBlock->SetVisibility(ESlateVisibility::Collapsed);
	}

	TextBlock->SetText(Text);
}

void URPGLeafLabledValue::SetLabelText(const FText& Text, bool bCollapse) const
{
	if (bCollapse)
	{
		LabelText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	LabelText->SetText(Text);
}

void URPGLeafLabledValue::SetValueText(const FText& Text, bool bCollapse) const
{
	if (bCollapse)
	{
		ValueText->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	ValueText->SetText(Text);
}

void URPGLeafLabledValue::NativePreConstruct()
{
	Super::NativePreConstruct();

	FSlateFontInfo FontInfo_Label = LabelText->GetFont();
	FontInfo_Label.Size = FontSize_Label;

	LabelText->SetFont(FontInfo_Label);

	FSlateFontInfo FontInfo_Value = ValueText->GetFont();
	FontInfo_Value.Size = FontSize_Value;

	ValueText->SetFont(FontInfo_Value);
}
