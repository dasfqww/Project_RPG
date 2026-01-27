// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Composite/RPGLeafImage.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"

void URPGLeafImage::SetImage(UTexture2D* Texture) const
{
	IconImage->SetBrushFromTexture(Texture);
}

void URPGLeafImage::SetBoxSize(const FVector2D& Size) const
{
	SizeBox->SetWidthOverride(Size.X);
	SizeBox->SetHeightOverride(Size.Y);
}

void URPGLeafImage::SetImageSize(const FVector2D& Size) const
{
	IconImage->SetDesiredSizeOverride(Size);
}

FVector2D URPGLeafImage::GetImageSize() const
{
	return IconImage->GetDesiredSize();
}
