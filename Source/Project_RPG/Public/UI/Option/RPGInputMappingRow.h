#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "RPGInputMappingRow.generated.h"

class UTextBlock;
class UInputKeySelector;
class UImage;
class UDataAsset_KeyIconConfig;
class UDataAsset_InputConfig;
class UInputAction;
class UInputMappingContext;
struct FRPGInputOptionBinding;

DECLARE_MULTICAST_DELEGATE(FOnRPGInputBindingChanged);

/**
 * 엔진의 FInputKeySelected 구조체가 인식이 안 되는 경우를 대비해 
 * 여기서 직접 정의하거나, 델리게이트 서명을 위해 선언합니다.
 */
USTRUCT(BlueprintType)
struct FRPGInputKeySelected
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Key")
	FKey Key;
};

/**
 * 키 바인딩 설정의 개별 항목을 나타내는 위젯
 */
UCLASS()
class PROJECT_RPG_API URPGInputMappingRow : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	/** Success notification; refresh requests do not imply a successful apply/save. */
	FOnRPGInputBindingChanged OnBindingChanged;
	/** Re-read the authoritative list after a completed attempt, including failures after a live change. */
	FOnRPGInputBindingChanged OnBindingRefreshRequested;
	/** Initialize a value model with the authored identities that must still match when a key is selected. */
	void InitializeOptionBinding(const FRPGInputOptionBinding& Binding, const UDataAsset_KeyIconConfig* InIconConfig);
	void InitializeBindingRow(const FGameplayTag& InTag, const FText& InActionName,
		const FKey& InDefaultKey, const FKey& InCurrentKey,
		const UDataAsset_KeyIconConfig* InIconConfig, bool bCanRemap);
	void RefreshDisplayedKey();

	UFUNCTION(BlueprintPure, Category = "Input")
	FKey GetCurrentBindingKey() const { return CurrentKey; }

	UFUNCTION(BlueprintPure, Category = "Input")
	bool IsSelectingKey() const;

	/** End capture without applying or saving a binding. Does not change another widget's focus. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void CancelKeySelection();

	UFUNCTION(BlueprintCallable, Category = "Input")
	bool ResetToDefault();

	/** True while this row can restore its authored default binding. */
	UFUNCTION(BlueprintPure, Category = "Input")
	bool CanResetToDefault() const;

	UPROPERTY(BlueprintReadOnly, Category = "Input")
	FText LastMappingError;

	// Legacy initialization keeps the primary binding contract.
	void InitializeRow(const FGameplayTag& InTag, const FText& InActionName, const FKey& InCurrentKey, const UDataAsset_KeyIconConfig* InIconConfig);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;

	UFUNCTION()
	void HandleSelectingKeyChanged();
	bool ApplySelectedKey(FKey Key);
	void SetMappingError(const FText& Error);
	void SynchronizeSelector();
	FReply HandleResetButtonClicked();
	FText GetNativeFeedbackText() const;

	// 엔진의 델리게이트 서명(FInputKeySelected)을 받기 위한 함수
	UFUNCTION()
	void HandleKeySelected(FInputChord SelectedKey);

	// 아이콘 업데이트 헬퍼
	void UpdateKeyIcon(const FKey& InKey);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ActionNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputKeySelector> KeySelector;

	// 키 아이콘을 표시할 이미지 위젯 (WBP에서 이 이름으로 Image 추가 필요)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyIconImage;

	UPROPERTY()
	TObjectPtr<const UDataAsset_KeyIconConfig> IconConfig;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MappingErrorText;

	/** Disable when a Blueprint supplies its own reset control. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Presentation")
	bool bShowNativeResetButton = true;

	/** Inline feedback is provided even when MappingErrorText is not bound. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input|Presentation")
	bool bShowNativeMappingFeedback = true;

	FGameplayTag ActionTag;
	FKey DefaultKey;
	FKey CurrentKey;
	TWeakObjectPtr<const UDataAsset_InputConfig> BoundInputConfig;
	TWeakObjectPtr<const UInputAction> BoundInputAction;
	TWeakObjectPtr<const UObject> BoundAdditionalInputProvider;
	TWeakObjectPtr<const UInputMappingContext> BoundSourceMappingContext;
	TWeakObjectPtr<const UInputMappingContext> BoundAppliedMappingContext;
	bool bBoundAdditionalMapping = false;
	uint64 BindingRevision = 0;
	bool bCanChangeKey = true;
	bool bSynchronizingSelector = false;
	bool bApplyingBinding = false;
	bool bRowConstructed = false;
};