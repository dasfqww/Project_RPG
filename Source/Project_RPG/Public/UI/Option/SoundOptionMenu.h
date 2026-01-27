// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "Type/RPGEnumTypes.h"
#include "SoundOptionMenu.generated.h"

class USlider;
class UCheckBox;
class URPGCheckBox;
class UTextBlock;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API USoundOptionMenu : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	USoundOptionMenu();

	virtual void NativeConstruct() override;

	void InitWidgetMaps();

	void InitializeFromSavedSettings();

	UFUNCTION()
	void OnMasterVolumeChanged(float Value);

	UFUNCTION()
	void OnMasterMuteChanged(bool bMute);

	UFUNCTION()
	void OnVolumeChanged(float Value);

	UFUNCTION(BlueprintCallable)
	void SetVolumeRatioText(UTextBlock* TextBlock, float Value);

	UFUNCTION()
	void OnMuteChanged(bool bMute, URPGCheckBox* CheckBox);

	UFUNCTION(BlueprintCallable)
	float GetSliderValue(USlider* InSlider);

	UFUNCTION(BlueprintCallable)
	void ApplySettings();

protected:
	// 슬라이더와 그에 해당하는 음향 타입을 매핑
	TMap<ESoundType, USlider*> SoundTypeToSliderMap;
	TMap<ESoundType, URPGCheckBox*> SoundTypeToCheckBoxMap;
	//TMap<UCheckBox*, ESoundType> CheckBoxToSoundTypeMap;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USlider> MasterVolumeSlider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USlider> BGMVolumeSlider;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (BindWidget))
	TObjectPtr<USlider> EffectVolumeSlider;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> MasterMuteCheckBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGCheckBox> BGMMuteCheckBox;

	UPROPERTY( meta = (BindWidget))
	TObjectPtr<URPGCheckBox> EffectMuteCheckBox;

public:

};
