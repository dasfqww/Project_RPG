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
struct FRPGSoundOptionsControls;
struct FRPGSoundSettings;

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
	virtual void NativeDestruct() override;

	void InitWidgetMaps();

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void InitializeFromSavedSettings();

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void ResetToDefaults();

	UPROPERTY(BlueprintReadOnly, Category = "Audio")
	FText LastSaveMessage;

	UFUNCTION(BlueprintPure, Category = "Audio")
	bool DidLastSaveFail() const { return bLastSaveFailed; }

	UFUNCTION()
	void OnMasterVolumeChanged(float Value);

	UFUNCTION()
	void OnMasterMuteChanged(bool bMute);

	UFUNCTION()
	void OnVolumeChanged(float Value);

	UFUNCTION()
	void OnBGMVolumeChanged(float Value);

	UFUNCTION()
	void OnEffectVolumeChanged(float Value);

	UFUNCTION(BlueprintCallable)
	void SetVolumeRatioText(UTextBlock* TextBlock, float Value);

	UFUNCTION()
	void OnMuteChanged(bool bMute, URPGCheckBox* CheckBox);

	UFUNCTION(BlueprintCallable)
	float GetSliderValue(USlider* InSlider);

	UFUNCTION(BlueprintCallable)
	void ApplySettings();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	/** Disable when a Blueprint provides its own sound action controls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Audio|Presentation")
	bool bShowNativeSoundActions = true;

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
	FRPGSoundOptionsControls GetRuntimeControls() const;
	void UnbindControls();
	void ClearSaveFeedback();
	void ApplyChannelVolume(ESoundType Type, float Value);
	void ApplySoundChange(TFunctionRef<void(FRPGSoundSettings&)> Update);
	friend struct FRPGSoundOptionsTestAccess;
	bool CanInteractWithOptions() const;
	FText GetVolumeSummary() const;
	FReply HandleSaveClicked();
	FReply HandleDefaultsClicked();
	bool bSynchronizingOptions = false;
	bool bApplyingOptions = false;
	bool bMenuConstructed = false;
	bool bLastSaveFailed = false;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> MasterMuteCheckBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGCheckBox> BGMMuteCheckBox;

	UPROPERTY( meta = (BindWidget))
	TObjectPtr<URPGCheckBox> EffectMuteCheckBox;

public:

};
