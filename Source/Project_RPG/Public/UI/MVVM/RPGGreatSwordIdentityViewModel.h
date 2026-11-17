#pragma once

#include "UI/MVVM/RPGViewModelBase.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"
#include "RPGGreatSwordIdentityViewModel.generated.h"

class UAbilitySystemComponent;
class URPGPlayerSkillComponent;
class URPGPlayerClassComponent;
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

/** Observes the current identity owner; never grants, activates or spends gauge. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGGreatSwordIdentityViewModel : public URPGViewModelBase
{
    GENERATED_BODY()
public:
    void SetSource(UAbilitySystemComponent* Next);
    void Activate();
    void Deactivate();
    virtual void BeginDestroy() override;
    FText GetIdentityText() const;
    FGameplayTag GetActivationInputTag() const;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Identity") FRPGGreatSwordIdentityPresentation Displayed;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Identity") float GaugePercent = 0.f;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Identity") FText ModeText;
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category="Identity") FText PercentText;
    DECLARE_MULTICAST_DELEGATE(FOnPresentationChanged);
    FOnPresentationChanged OnPresentationChanged;
private:
    void BindSource();
    void UnbindSource();
    void Refresh();
    void OnAttributeChanged(const FOnAttributeChangeData& Change);
    void OnModeChanged(FGameplayTag Tag, int32 Count);
    UFUNCTION() void OnSkillDataChanged(FGameplayTag Tag);
    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    TWeakObjectPtr<UAbilitySystemComponent> Source;
    TWeakObjectPtr<AActor> SourceActor;
    TWeakObjectPtr<URPGPlayerSkillComponent> Growth;
    TWeakObjectPtr<URPGPlayerClassComponent> ClassOwner;
    FDelegateHandle GaugeChanged, MaximumChanged, HealthChanged, ModeChanged, ClassChanged;
    bool bActive = false;
};
