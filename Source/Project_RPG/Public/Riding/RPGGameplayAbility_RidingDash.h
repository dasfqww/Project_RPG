#pragma once

#include "Abilities/GameplayAbility.h"
#include "Riding/RPGRidingAbilityPolicy.h"
#include "Riding/RPGRidingDashAdmission.h"
#include "Riding/RPGRidingDashGrant.h"
#include "Riding/RPGRidingDashIntent.h"
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
    void BeginServer(const FRPGRidingDashIntent& Intent);
    void ReceiveIntent(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag);
    void CancelIntent();
    void WatchExecution();
    void RidingChanged(const struct FRPGRidingRepState& State);
    TWeakObjectPtr<URPGRidingComponent> Riding;
    FRPGRidingDashTicket Ticket;
    FRPGRidingStateKey StartingKey;
    FRPGRidingDashActivation Context;
    FDelegateHandle ChangedHandle, DataHandle, CancelHandle;
    FTimerHandle WatchTimer;
    double IntentDeadline = 0;
    bool bStarting = false, bWaitingForIntent = false;
};
