// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "DragItemVisual.generated.h"

class UBorder;
class UImage;
class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDragItemVisual : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Drag Item Visual", meta = (BindWidget))
	TObjectPtr<UBorder> ItemBorder;

	UPROPERTY(VisibleAnywhere, Category = "Drag Item Visual", meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(EditDefaultsOnly, Category = "Drag Item Visual", meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemQuantity;
protected:
	
};
