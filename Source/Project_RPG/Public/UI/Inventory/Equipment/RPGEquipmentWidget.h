// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGEquipmentWidget.generated.h"

struct FGameplayTag;
class URPGEquippedGridSlot;
class URPGSpatialInventory;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGEquipmentWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	

private:
	UPROPERTY()
	TArray<TObjectPtr<URPGEquippedGridSlot>> EquippedGridSlots;

	UPROPERTY()
	TSubclassOf<URPGSpatialInventory> SpatialInventoryClass;

	UPROPERTY()
	TObjectPtr<URPGSpatialInventory> SpatialInventory;

	UFUNCTION()
	void EquippedGridSlotClicked(URPGEquippedGridSlot* EquippedGridSlot, const FGameplayTag& EquipmentTag);
	
	bool CanEquipHoverItem(URPGEquippedGridSlot* EquippedGridSlot,
		const FGameplayTag& EquipmentTypeTag) const;
};
