#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "TimerManager.h"
#include "RPGInputOptionMenu.generated.h"

class UScrollBox;
class URPGInputMappingRow;
class UDataAsset_InputConfig;
class UDataAsset_KeyIconConfig;
class ARPGPlayerController;
class APlayerController;
class ULocalPlayer;

/**
 * 키 바인딩 설정 전체 리스트를 보여주는 메뉴 위젯
 */
UCLASS()
class PROJECT_RPG_API URPGInputOptionMenu : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;

	// UI 리스트 새로고침
	UFUNCTION(BlueprintCallable, Category = "Input")
	void RefreshMappings();

	void RefreshDisplayedKeys();

	/** Last individual binding error remains readable even when filters hide its row. */
	UFUNCTION(BlueprintPure, Category = "Input")
	FText GetLastBindingChangeError() const { return LastBindingChangeError; }

	UFUNCTION(BlueprintCallable, Category = "Input")
	void CancelPendingKeySelection();

	UFUNCTION(BlueprintPure, Category = "Input")
	bool HasPendingKeySelection() const;

	/** Includes editable authored bindings hidden by presentation filters. */
	UFUNCTION(BlueprintCallable, Category = "Input|Defaults")
	bool RequestRestoreAllDefaults();

	UFUNCTION(BlueprintCallable, Category = "Input|Defaults")
	bool ConfirmRestoreAllDefaults();

	UFUNCTION(BlueprintCallable, Category = "Input|Defaults")
	void CancelRestoreAllDefaults();

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	bool IsAwaitingDefaultRestoration() const { return !PendingDefaultBindings.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	bool CanRestoreAllDefaults() const;

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	int32 GetRestorableBindingCount() const;

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	int32 GetPendingDefaultRestorationCount() const { return PendingDefaultBindings.Num(); }

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	FText GetRestoreDefaultsStatusMessage() const { return RestoreDefaultsStatusMessage; }

	UFUNCTION(BlueprintPure, Category = "Input|Defaults")
	bool DidLastDefaultRestorationFail() const { return bLastRestoreFailed; }

	UFUNCTION(BlueprintCallable, Category = "Input|Filter")
	void SetBindingSearchText(const FText& InSearchText);

	UFUNCTION(BlueprintCallable, Category = "Input|Filter")
	void SetBindingDeviceFilter(ERPGInputOptionDeviceFilter InDeviceFilter);

	UFUNCTION(BlueprintCallable, Category = "Input|Filter")
	void SetShowChangedBindingsOnly(bool bInChangedOnly);

	UFUNCTION(BlueprintCallable, Category = "Input|Filter")
	void ResetBindingFilters();

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	bool CanChangeBindingFilters() const;

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	FText GetBindingSearchText() const { return BindingSearchText; }

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	ERPGInputOptionDeviceFilter GetBindingDeviceFilter() const { return BindingDeviceFilter; }

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	bool GetShowChangedBindingsOnly() const { return bShowChangedBindingsOnly; }

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	int32 GetVisibleBindingCount() const { return VisibleBindingCount; }

	UFUNCTION(BlueprintPure, Category = "Input|Filter")
	int32 GetTotalBindingCount() const { return TotalBindingCount; }

private:
	struct FBindingRow
	{
		TWeakObjectPtr<URPGInputMappingRow> Widget;
		FRPGInputOptionBinding Binding;
		ESlateVisibility UnfilteredVisibility = ESlateVisibility::SelfHitTestInvisible;
	};

	bool CanManageDefaultRestoration() const;
	void FocusDefaultRestorationControl(const TSharedPtr<SWidget>& Target) const;
	FReply HandleRestoreDefaultsClicked();
	FReply HandleConfirmDefaultsClicked();
	FReply HandleCancelDefaultsClicked();
	FText GetDefaultRestorationPrompt() const;

	TArray<FRPGInputOptionBinding> PendingDefaultBindings;
	TWeakObjectPtr<ARPGPlayerController> PendingDefaultsController;
	TWeakObjectPtr<UWorld> PendingDefaultsWorld;
	TWeakPtr<SWidget> NativeRestoreDefaultsButton;
	TWeakPtr<SWidget> NativeDefaultsCancelButton;
	TWeakPtr<SWidget> NativeDefaultsConfirmationPanel;
	FText RestoreDefaultsStatusMessage;
	uint64 DefaultRestorationRevision = 0;
	bool bRestoringDefaults = false;
	bool bLastRestoreFailed = false;

	void HandleInputOptionMappingsChanged(ULocalPlayer* Player, UWorld* World);
	void UnbindInputOptionsChanges();
	void CancelScheduledInputRefresh();
	void RequestInputMappingRefresh();
	void FlushInputMappingRefresh(uint64 Generation, TWeakObjectPtr<UWorld> ExpectedWorld,
		TWeakObjectPtr<ULocalPlayer> ExpectedPlayer, TWeakObjectPtr<APlayerController> ExpectedController);
	void CancelDefaultsForInputChange();
	FDelegateHandle InputOptionsChangedHandle;
	FTimerHandle InputRefreshTimer;
	TWeakObjectPtr<UWorld> InputRefreshWorld;
	TWeakObjectPtr<ULocalPlayer> InputRefreshPlayer;
	TWeakObjectPtr<APlayerController> InputRefreshController;
	uint64 InputRefreshGeneration = 0;
	bool bInputRefreshPending = false;
	bool bInputRefreshPaused = false;

	void HandleInputMenuVisibilityChanged(ESlateVisibility NewVisibility);
	void HandleBindingRefreshRequested(TWeakObjectPtr<URPGInputMappingRow> RequestedRow);
	FText LastBindingChangeError;
	void UnbindRows();
	void ApplyRowFilters();
	FReply HandleDeviceFilterClicked(ERPGInputOptionDeviceFilter DeviceFilter);
	FReply HandleResetFiltersClicked();
	FText GetBindingFilterSummary() const;

	TArray<FBindingRow> BindingRows;
	TWeakPtr<SWidget> NativeFilterFocusTarget;
	FText BindingSearchText;
	ERPGInputOptionDeviceFilter BindingDeviceFilter = ERPGInputOptionDeviceFilter::All;
	bool bShowChangedBindingsOnly = false;
	int32 VisibleBindingCount = 0;
	int32 TotalBindingCount = 0;
	uint64 BindingRowsRevision = 0;
	bool bApplyingRowFilters = false;
	bool bRefreshingMappings = false;
	bool bRefreshingDisplayedKeys = false;
	bool bMenuConstructed = false;
	bool bCancellingSelections = false;
	FDelegateHandle MenuVisibilityHandle;
	TWeakObjectPtr<ARPGPlayerController> BuiltRowsController;
	TWeakObjectPtr<UClass> BuiltRowWidgetClass;
	TWeakObjectPtr<UDataAsset_KeyIconConfig> BuiltKeyIconConfig;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;

	/** Disable when a Blueprint supplies its own filter controls. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Presentation")
	bool bShowNativeBindingFilters = true;

	/** Custom Blueprint controls can call the same request/confirm/cancel APIs. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Presentation")
	bool bShowNativeDefaultsActions = true;

	// 개별 항목으로 생성할 위젯 클래스 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<URPGInputMappingRow> RowWidgetClass;

	// 입력 설정 데이터 에셋 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UDataAsset_InputConfig> InputConfig;

	// 키 아이콘 데이터 에셋 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UDataAsset_KeyIconConfig> KeyIconConfig;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> MappingScrollBox;
};