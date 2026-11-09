#pragma once

#include "Abilities/GameplayAbility.h"
#include "Riding/RPGRidingAbilityPolicy.h"
#include "Riding/RPGRidingDashAdmission.h"
#include "RPGGameplayAbility_RidingDash.generated.h"

class URPGRidingComponent;

/** Native Riding capability. Owns GAS lifetime; the component owns motion and session cooldown. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGGameplayAbility_RidingDash final : public UGameplayAbility, public IRPGRidingAbilityPolicy
{
    GENERATED_BODY()
public:
    URPGGameplayAbility_RidingDash();
    virtual bool IsAutomaticRidingExempt() const override { return false; }
    virtual bool IsRidingActionAbility() const override { return true; }
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayTagContainer* Source = nullptr, const FGameplayTagContainer* Target = nullptr,
        FGameplayTagContainer* Failures = nullptr) const override;
protected:
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;
private:
    void RidingChanged(const struct FRPGRidingRepState& State);
    TWeakObjectPtr<URPGRidingComponent> Riding;
    FRPGRidingDashTicket Ticket;
    FRPGRidingStateKey StartingKey;
    FDelegateHandle ChangedHandle;
    bool bStarting = false;
};
