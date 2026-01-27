// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/GridSlot/RPGGridSlot.h"
#include "GameplayTagContainer.h"
#include "RPGEquippedGridSlot.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEquippedGridSlotClicked, URPGEquippedGridSlot*, GridSlot, const FGameplayTag&, EquipmentTag);

class UImage;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGEquippedGridSlot : public URPGGridSlot
{
	GENERATED_BODY()
public:
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	FOnEquippedGridSlotClicked OnEquippedGridSlotClicked;

private:
	UPROPERTY(EditAnywhere, Category = "EquipmentWidget", meta = (Categories = "GameItems.Equipment"))
	FGameplayTag EquipmentTag;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> GrayedOutImage;

	void SetSlotTextureByHoverItem(EGridSlotState SlotState, ESlateVisibility InVisibility);
};
