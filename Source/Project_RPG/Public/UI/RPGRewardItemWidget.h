// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGRewardItemWidget.generated.h"

class URPGItemBase;
class UBorder;
class UImage;
class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGRewardItemWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGRewardItemWidget();

	virtual void NativeConstruct() override;

	void InitializeRewardItems();

protected:
	UPROPERTY()
	TObjectPtr<URPGItemBase> ItemReference;

	UPROPERTY(VisibleAnywhere, Category = "Item Slot", meta = (BindWidget))
	TObjectPtr<UBorder> ItemBorder;

	UPROPERTY(VisibleAnywhere, Category = "Item Slot", meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	UPROPERTY(EditDefaultsOnly, Category = "Item Slot", meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemQuantity;

public:
	FORCEINLINE URPGItemBase* GetItemReference() const { return ItemReference; }
	FORCEINLINE void SetItemReference(URPGItemBase* InItem) { ItemReference = InItem; }
};
