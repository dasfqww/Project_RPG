#pragma once

#include "Blueprint/UserWidget.h"
#include "RPGPlayerVitalsWidget.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UTextBlock;
struct FOnAttributeChangeData;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGPlayerVitalsSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bHasSource = false;
	UPROPERTY(BlueprintReadOnly) float Health = 0.f;
	UPROPERTY(BlueprintReadOnly) float MaxHealth = 0.f;
	UPROPERTY(BlueprintReadOnly) float Mana = 0.f;
	UPROPERTY(BlueprintReadOnly) float MaxMana = 0.f;
	UPROPERTY(BlueprintReadOnly) float HealthFraction = 0.f;
	UPROPERTY(BlueprintReadOnly) float ManaFraction = 0.f;
};

/** HP/MP presentation only. Reads current GAS values and observes changes without polling. */
UCLASS(meta=(DisableNativeTick))
class PROJECT_RPG_API URPGPlayerVitalsWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** Explicit source for previews/hosts. Safe before Construct; null clears the view. */
	UFUNCTION(BlueprintCallable, Category="UI|Vitals")
	void SetVitalsSource(UAbilitySystemComponent* Source);

	UFUNCTION(BlueprintPure, Category="UI|Vitals")
	FRPGPlayerVitalsSnapshot GetDisplayedVitals() const { return DisplayedVitals; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void BeginDestroy() override;

	/** Automatically follows the local owning controller's current Pawn when constructed. */
	UPROPERTY(EditDefaultsOnly, Category="Vitals") bool bAutoBindOwningPawn = true;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Vitals") TObjectPtr<UProgressBar> HealthBar;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Vitals") TObjectPtr<UProgressBar> ManaBar;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Vitals") TObjectPtr<UTextBlock> HealthText;
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Vitals") TObjectPtr<UTextBlock> ManaText;

	UFUNCTION(BlueprintImplementableEvent, Category="UI|Vitals")
	void OnVitalsPresentationChanged();

private:
	friend struct FRPGPlayerVitalsWidgetTestAccess;
	void UnbindSource();
	void BindAndRefresh();
	void UnbindController();
	void RefreshPresentation();
	void ApplyPresentation();
	void OnAttributeChanged(const FOnAttributeChangeData& Change);
	UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
	UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);

	TWeakObjectPtr<UAbilitySystemComponent> VitalsSource;
	TWeakObjectPtr<UAbilitySystemComponent> BoundSource;
	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<APlayerController> BoundController;
	FDelegateHandle HealthHandle, MaxHealthHandle, ManaHandle, MaxManaHandle;
	UPROPERTY(Transient) FRPGPlayerVitalsSnapshot DisplayedVitals;
	bool bConstructed = false;
};
