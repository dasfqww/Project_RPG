// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGLeafText.h"
#include "Components/TextBlock.h"

void URPGLeafText::SetText(const FText& Text) const
{
	LeafText->SetText(Text);
}

void URPGLeafText::NativePreConstruct()
{
	Super::NativePreConstruct();

	FSlateFontInfo FontInfo = LeafText->GetFont();
	FontInfo.Size = FontSize;

	LeafText->SetFont(FontInfo);
}
