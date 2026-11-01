#pragma once

#include "Blueprint/UserWidget.h"
#include "RPGCombatContributionWidget.generated.h"

class UBorder;
class UProgressBar;
class UTextBlock;
class UVerticalBox;
struct FRPGCombatContributionState;

/** Local, read-only view of the locked target's replicated mechanic meters. */
UCLASS()
class PROJECT_RPG_API URPGCombatContributionWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, Category="Combat|Contribution")
	AActor* GetDisplayedTarget() const { return DisplayedTarget.Get(); }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	virtual void NativeDestruct() override;
private:
	friend struct FRPGCombatContributionWidgetTestAccess;
	void RefreshCurrentTarget();
	void RefreshFromTarget(AActor* Target);
	void ClearPresentation();
	void ShowState(const FRPGCombatContributionState& State);
	UPROPERTY(Transient) TObjectPtr<UBorder> Card;
	UPROPERTY(Transient) TArray<TObjectPtr<UVerticalBox>> MeterRows;
	UPROPERTY(Transient) TArray<TObjectPtr<UProgressBar>> Meters;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Labels;
	TWeakObjectPtr<AActor> DisplayedTarget;
	bool bAttached = false;
};
