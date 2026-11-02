#pragma once

#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGGreatSwordIdentityWidget.generated.h"

class UAbilitySystemComponent;
class URPGPlayerSkillComponent;
class URPGPlayerClassComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;
struct FOnAttributeChangeData;

USTRUCT(BlueprintType)
struct FRPGGreatSwordIdentityPresentation
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bHasSource = false;
    UPROPERTY(BlueprintReadOnly) bool bUnlocked = false;
    UPROPERTY(BlueprintReadOnly) bool bBerserk = false;
    UPROPERTY(BlueprintReadOnly) float Gauge = 0.f;
    UPROPERTY(BlueprintReadOnly) float Maximum = 0.f;
    UPROPERTY(BlueprintReadOnly) float Fraction = 0.f;
};

/** Read-only presentation of the current Pawn's replicated identity attributes and mode tag. */
UCLASS(meta=(DisableNativeTick))
class PROJECT_RPG_API URPGGreatSwordIdentityWidget : public URPGWidgetBase
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="UI|Identity") void SetIdentitySource(UAbilitySystemComponent* Source);
    UFUNCTION(BlueprintPure, Category="UI|Identity") FRPGGreatSwordIdentityPresentation GetDisplayedIdentity() const { return Displayed; }
    UFUNCTION(BlueprintPure, Category="UI|Identity") FText GetIdentityText() const;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void BeginDestroy() override;
    UPROPERTY(EditDefaultsOnly, Category="Identity") bool bAutoBindOwningPawn = true;
    // Keep the authored status-bar template and its skin; only its primitive values change.
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UUserWidget> WBP_IdentityGaugeBar;
    UPROPERTY(Transient, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> InterpUp;
private:
    friend struct FRPGGreatSwordIdentityWidgetTestAccess;
    void UnbindSource();
    void UnbindController();
    void BindSource();
    void Refresh();
    void Present(bool bModeChanged);
    void OnAttributeChanged(const FOnAttributeChangeData& Change);
    void OnModeChanged(FGameplayTag Tag, int32 Count);
    UFUNCTION() void OnSkillDataChanged(FGameplayTag Tag);
    UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    TWeakObjectPtr<UAbilitySystemComponent> Source;
    TWeakObjectPtr<AActor> SourceActor;
    TWeakObjectPtr<URPGPlayerSkillComponent> Growth;
    TWeakObjectPtr<URPGPlayerClassComponent> ClassOwner;
    TWeakObjectPtr<APlayerController> Controller;
    FDelegateHandle GaugeChanged, MaximumChanged, HealthChanged, ModeChanged, ClassChanged;
    UPROPERTY(Transient) FRPGGreatSwordIdentityPresentation Displayed;
    bool bConstructed = false;
};
