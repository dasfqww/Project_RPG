#pragma once

#include "Blueprint/UserWidget.h"
#include "UI/Progress/RPGActionProgressTypes.h"
#include "RPGActionProgressWidget.generated.h"

class UPlayerUIComponent;
class UProgressBar;
class UTextBlock;
class UImage;

/** Presentation only. The owning HUD supplies/replaces the source when its Pawn changes. */
UCLASS(meta=(DisableNativeTick))
class PROJECT_RPG_API URPGActionProgressWidget : public UUserWidget
{

	GENERATED_BODY()
public:
	/** Safe before Construct; null clears the display. Does not end the underlying action. */
	UFUNCTION(BlueprintCallable, Category="UI|Action Progress")
	void SetProgressSource(UPlayerUIComponent* Source);

	UFUNCTION(BlueprintPure, Category="UI|Action Progress")
	FRPGActionProgressSnapshot GetDisplayedProgress() const { return DisplayedProgress; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;

	/** Optional authored controls; without an authored tree a basic layout is provided. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UProgressBar> ProgressBar;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UTextBlock> ActionLabel;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UTextBlock> TimeLabel;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UTextBlock> StageLabel;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UTextBlock> PhaseLabel;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Action Progress") TObjectPtr<UImage> ActionIcon;

	/** Read the current snapshot here; cosmetic BP callbacks may also replace the source. */
	UFUNCTION(BlueprintImplementableEvent, Category="UI|Action Progress")
	void OnProgressPresentationChanged();

private:
	friend struct FRPGActionProgressWidgetTestAccess;
	void Unsubscribe();
	void BindAndRefresh();
	void RefreshPresentation();
	void ApplyPresentation();
	TWeakObjectPtr<UPlayerUIComponent> ProgressSource;
	FDelegateHandle ChangeHandle;
	UPROPERTY(Transient) FRPGActionProgressSnapshot DisplayedProgress;
	TSoftObjectPtr<UTexture2D> DisplayedIcon;
	bool bConstructed = false;
};
