// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Composite/RPGLeaf.h"
#include "RPGLeafImage.generated.h"

class UImage;
class USizeBox;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGLeafImage : public URPGLeaf
{
	GENERATED_BODY()
public:
	void SetImage(UTexture2D* Texture) const;
	void SetBoxSize(const FVector2D& Size) const;
	void SetImageSize(const FVector2D& Size) const;
	FVector2D GetImageSize() const;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox;
};
