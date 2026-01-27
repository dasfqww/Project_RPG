// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Composite/RPGLeaf.h"
#include "RPGLeafLabledValue.generated.h"

class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGLeafLabledValue : public URPGLeaf
{
	GENERATED_BODY()
public:

	void SetLablelValueText(UTextBlock* TextBlock, const FText& Text, bool bCollapse) const;
	void SetLabelText(const FText& Text, bool bCollapse) const;
	void SetValueText(const FText& Text, bool bCollapse) const;
	virtual void NativePreConstruct() override;

private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ValueText;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 FontSize_Label = 12;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 FontSize_Value = 18;
};
