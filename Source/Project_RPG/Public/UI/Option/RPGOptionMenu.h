// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGOptionMenu.generated.h"

class USoundOptionMenu;
class UButton;
class UWidgetSwitcher;
class USoundBase;

UENUM(BlueprintType)
enum class ESettingTabType : uint8
{
	Sound,
	Graphics,
	Gameplay,
	Controls
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGOptionMenu : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	URPGOptionMenu();

	virtual void NativeConstruct() override;

	void InitButtonMap();

	UFUNCTION()
	void OnTabButtonHovered();

	UFUNCTION()
	void OnTabButtonClicked();

private:
	UPROPERTY(EditDefaultsOnly, Category = "Sound", meta = (AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> HoverButtonSound;
	
	UPROPERTY(EditDefaultsOnly, Category = "Sound", meta = (AllowPrivateAccess="true"))
	TObjectPtr<USoundBase> ClickButtonSound;

	UPROPERTY()
	TMap<ESettingTabType, TObjectPtr<UButton>> TabButtonMap;

	UPROPERTY()
	TMap<ESettingTabType, int32> TabIndexMap;

	UPROPERTY(VisibleAnywhere, Category = "Tab Button", meta = (BindWidget))
	TObjectPtr<UButton> SoundOptionButton;
	
	UPROPERTY(VisibleAnywhere, Category = "Tab Button", meta = (BindWidget))
	TObjectPtr<UButton> GraphicOptionButton;

	UPROPERTY(VisibleAnywhere, Category = "Tab Button", meta = (BindWidget))
	TObjectPtr<UButton> InputOptionButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> TabSwitcher;
};
