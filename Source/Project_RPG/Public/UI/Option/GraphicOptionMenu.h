// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GraphicOptionMenu.generated.h"

class UComboBoxString;
class UCheckBox;
class UButton;
class URPGGameUserSettings;

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
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void RemoveFromParent() override;

	UFUNCTION(BlueprintPure, Category = "Graphics")
	bool IsAwaitingDisplayConfirmation() const;

	UFUNCTION(BlueprintPure, Category = "Graphics")
	int32 GetDisplayConfirmationSecondsRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Graphics")
	FText GetApplyStatusMessage() const;

	UFUNCTION(BlueprintCallable, Category = "Graphics")
	void ConfirmDisplayChanges();

	UFUNCTION(BlueprintCallable, Category = "Graphics")
	void RevertDisplayChanges();

	/** Also works when the optional GameFeature module is unloading. */
	void CancelPendingGraphicsChange();

	UFUNCTION(BlueprintCallable, Category = "Graphics")
	void InitializeOptions();

	UPROPERTY(BlueprintReadOnly, Category = "Graphics")
	FText LastApplyMessage;

	// 설정 적용 함수
	UFUNCTION(BlueprintCallable)
	void OnApplyButtonClicked();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnDeactivated() override;

	/** Disable when a Blueprint supplies its own graphics action controls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Graphics|Presentation")
	bool bShowNativeGraphicsActions = true;

private:
	bool CanUseOptions() const;
	bool CanInteractWithOptions() const;
	bool CanResolveDisplayConfirmation() const;
	void FocusNativeAction(bool bConfirmation);
	void HandleOptionsVisibilityChanged(ESlateVisibility NewVisibility);
	FReply HandleConfirmClicked();
	FReply HandleRevertClicked();
	FReply HandleApplyClicked();
	FReply HandleDiscardClicked();
	bool bSynchronizingOptions = false;
	bool bApplyingOptions = false;
	bool bLastApplyFailed = false;
	bool bMenuConstructed = false;
	bool bAwaitingConfirmation = false;
	FDelegateHandle VisibilityChangedHandle;
	TWeakObjectPtr<URPGGameUserSettings> PreviewSettings;
	TWeakPtr<SWidget> NativeApplyFocusTarget;
	TWeakPtr<SWidget> NativeConfirmFocusTarget;
	TWeakPtr<SWidget> NativeConfirmationActions;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> ResolutionComboBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UComboBoxString> WindowModeComboBox;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCheckBox> VSyncCheckBox;
};
