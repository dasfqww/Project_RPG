// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGItemSplitPopUp.generated.h"

class UButton;
class USlider;
class UTextBlock;
class USizeBox;

DECLARE_DYNAMIC_DELEGATE_TwoParams(FOnPopUpMenuSplit, int32, SplitAmount, int32, Index);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGItemSplitPopUp : public URPGWidgetBase
{
	GENERATED_BODY()
public:

	virtual void NativeOnInitialized() override;

	int32 GetSplitAmount() const;

	FOnPopUpMenuSplit OnSplit;

	UFUNCTION()
	void OnSplitAccepted();

	UFUNCTION()
	void OnSliderValueChanged(float Value);

	FVector2D GetBoxSize() const;

	void CollapseSplitButton();
	void SetSliderParams(const float Max, const float Value) const;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> AcceptButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USlider> SplitSlider;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SplitAmountText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<USizeBox> RootSizeBox;

	int32 GridIndex=INDEX_NONE;

public:
	FORCEINLINE void SetGridIndex(int32 Index) { GridIndex = Index; }
};
