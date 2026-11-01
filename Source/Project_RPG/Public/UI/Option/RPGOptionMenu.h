// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UI/RPGWidgetBase.h"
#include "UI/Option/RPGOptionsMenuRuntime.h"
#include "RPGOptionMenu.generated.h"

class USoundOptionMenu;
class UButton;
class UWidgetSwitcher;
class USoundBase;
class ULocalPlayer;

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
    virtual void NativeDestruct() override;
	virtual void RemoveFromParent() override;

	void InitButtonMap();

	UFUNCTION()
	void OnTabButtonHovered();

	UFUNCTION()
	void OnTabButtonClicked();

	void OnMenuShown();
	void OnMenuHidden();

	/** Back cancels key capture or provisional display changes before closing this menu. */
	UFUNCTION(BlueprintCallable, Category = "UI|Options")
	bool RequestBack();

	UFUNCTION(BlueprintCallable, Category = "UI|Options")
	bool CloseOptionsMenu();

	UFUNCTION(BlueprintCallable, Category = "UI|Options")
	bool SelectAdjacentTab(bool bNext = true);

	/** Borrowed Slate focus; the runtime validates its current visible/enabled path before restoring. */
	TSharedPtr<SWidget> GetRememberedOptionsFocus() const;
	UWidget* GetSelectedTabFocusTarget() const;

protected:
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
	virtual FReply NativeOnKeyUp(const FGeometry& Geometry, const FKeyEvent& Event) override;

	/** Disable when a Blueprint supplies its own back/tab shortcuts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI|Options|Navigation")
	bool bEnableNativeOptionNavigation = true;

private:
	void UnbindTabButtons();
	void SelectTab(ESettingTabType Tab);

	UFUNCTION()
	void OnGraphicsTabClicked();

	UFUNCTION()
	void OnSoundTabClicked();

	UFUNCTION()
	void OnControlsTabClicked();

	bool CanCloseOptionsMenu() const;
	bool CanHandleOptionsNavigation() const;
	bool HasPendingInputSelection() const;
	void RememberOptionsFocus();
	void CancelPendingInputSelections();
	void CancelGraphicsPreviews();
	void EndInputSession();
	void HandleMenuVisibilityChanged(ESlateVisibility NewVisibility);

	FRPGOptionsMenuInputState InputState;
	FDelegateHandle MenuVisibilityHandle;
	bool bEndingInputSession = false;
	bool bShowingMenu = false;
	TWeakPtr<SWidget> RememberedOptionsFocus;
	TWeakObjectPtr<ULocalPlayer> RememberedFocusPlayer;
	TWeakObjectPtr<UWorld> RememberedFocusWorld;
	int32 RememberedFocusTabIndex = INDEX_NONE;
	TSet<FKey> ConsumedNavigationKeys;
	bool bHandlingNavigation = false;
	bool bClosingOptions = false;

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

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class URPGInputOptionMenu> InputOptionMenu;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> TabSwitcher;
};
