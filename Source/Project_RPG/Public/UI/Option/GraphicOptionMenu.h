// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GraphicOptionMenu.generated.h"

class UComboBoxString;
class UCheckBox;
class UButton;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UGraphicOptionMenu : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	UGraphicOptionMenu();

	virtual void NativeConstruct() override;

	void InitializeOptions();

	// 설정 적용 함수
	UFUNCTION(BlueprintCallable)
	void OnApplyButtonClicked();

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ResolutionComboBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> WindowModeComboBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> VSyncCheckBox;
};
