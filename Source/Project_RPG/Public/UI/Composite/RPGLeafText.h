// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Composite/RPGLeaf.h"
#include "RPGLeafText.generated.h"

class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGLeafText : public URPGLeaf
{
	GENERATED_BODY()
public:
	void SetText(const FText& Text) const;
	virtual void NativePreConstruct() override;

private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> LeafText;

	UPROPERTY(EditAnywhere, Category = "Inventory")
	int32 FontSize = 12;
};
