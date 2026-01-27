// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGHoverItem.generated.h"

class UImage;
class UTextBlock;
class URPGItemBase;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGHoverItem : public URPGWidgetBase
{
	GENERATED_BODY()
public:

	void SetImageBrush(const FSlateBrush& Brush) const;
	void UpdateQuantity(int32 InQuantity);

	FGameplayTag GetItemTag() const;
	void SetIsStackable(bool bStackable);
	URPGItemBase* GetInvenItem() const;
	void SetInvenItem(URPGItemBase* Item);

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> QuantityText;

	int32 PrevGridIndex;
	FIntPoint GridDimensions;
	TWeakObjectPtr<URPGItemBase> InvenItem;
	bool bIsStackable = false;
	int32 Quantity = 0;

public:

	FORCEINLINE int32 GetQuantity() { return Quantity; }
	FORCEINLINE bool IsStackable() { return bIsStackable; }
	FORCEINLINE int32 GetPrevGridIndex() { return PrevGridIndex; }
	FORCEINLINE void SetPrevGridIndex(int32 Index) { PrevGridIndex=Index; }
	FORCEINLINE FIntPoint GetGridDimensions() { return GridDimensions; }
	FORCEINLINE void SetGridDimensions(const FIntPoint& Dimension) { GridDimensions=Dimension; }
	
};
