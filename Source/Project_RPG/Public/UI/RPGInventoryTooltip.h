// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "UI/Composite/RPGComposite.h"
#include "RPGInventoryTooltip.generated.h"

class UTextBlock;
class URPGInventoryItemSlot;
class USizeBox;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryTooltip : public URPGComposite
{
	GENERATED_BODY()
public:
	URPGInventoryTooltip();

	/*UPROPERTY(VisibleAnywhere)
	TObjectPtr<URPGInventoryItemSlot> InventorySlotBeingHovered;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemName;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemType;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DamageValue;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ArmorRating;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> UsageText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemDescription;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> MaxStackSize;*/
	
	////UPROPERTY(meta = (BindWidget))
	//TObjectPtr<UTextBlock> SellValue;

	/*UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> StackWeight;*/

	virtual void NativeConstruct() override;

	FVector2D GetBoxSize() const;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> SizeBox;


public:
	/*FORCEINLINE void SetInventorySlotBeingHovered(URPGInventoryItemSlot* NewInventorySlot) 
	{
		InventorySlotBeingHovered = NewInventorySlot;
	}*/
};
