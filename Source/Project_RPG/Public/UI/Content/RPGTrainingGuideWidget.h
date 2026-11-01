#pragma once

#include "Blueprint/UserWidget.h"
#include "RPGTrainingGuideWidget.generated.h"

class APawn;
class ARPGPlayerController;
class UBorder;
class UTextBlock;
struct FRPGTrainingObjectiveProgress;

/** Read-only local guide for the current Pawn's server-approved control practice. */
UCLASS()
class PROJECT_RPG_API URPGTrainingGuideWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="Progression|Training")
	APawn* GetDisplayedPawn() const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual void NativeDestruct() override;
private:
	friend struct FRPGTrainingGuideWidgetTestAccess;
	void RefreshCurrentPawn();
	void ClearPresentation();
	void RefreshCompletionActions(bool bPracticeComplete);
	void ClearCompletionActions();
	void ShowProgress(const FRPGTrainingObjectiveProgress& Progress, const ARPGPlayerController& Controller);
	FText ReadControlHint(int32 Goal, const ARPGPlayerController& Controller) const;
	UPROPERTY(Transient) TObjectPtr<UBorder> Card;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Title;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Hint;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Goals;
	UPROPERTY(Transient) TObjectPtr<class URPGDungeonResultActionsWidget> CompletionActions;
	FString CompletionCharacterId;
	FString CompletionSessionId;
	TWeakObjectPtr<APawn> DisplayedPawn;
	bool bAttached = false;
};
