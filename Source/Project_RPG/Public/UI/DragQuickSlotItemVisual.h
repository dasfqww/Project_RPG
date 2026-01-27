// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "DragQuickSlotItemVisual.generated.h"

class UImage;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UDragQuickSlotItemVisual : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Drag Item Visual", meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;
};
