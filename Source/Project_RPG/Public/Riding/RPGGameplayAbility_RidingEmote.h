#pragma once
#include "Abilities/GameplayAbility.h"
#include "Riding/RPGRidingAbilityPolicy.h"
#include "Riding/RPGRidingEmoteAdmission.h"
#include "RPGGameplayAbility_RidingEmote.generated.h"

class URPGRidingComponent;
/** Cosmetic server action. Grants no movement window, damage, control or immunity. */
UCLASS(NotBlueprintable)
class PROJECT_RPG_API URPGGameplayAbility_RidingEmote final : public UGameplayAbility, public IRPGRidingAbilityPolicy
{
    GENERATED_BODY()
public:
    URPGGameplayAbility_RidingEmote();
    virtual bool IsAutomaticRidingExempt() const override { return false; }
    virtual bool IsRidingActionAbility() const override { return true; }
    virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayTagContainer* Source=nullptr, const FGameplayTagContainer* Target=nullptr, FGameplayTagContainer* Failures=nullptr) const override;
protected:
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, bool Replicate, bool Cancelled) override;
private:
    void RidingChanged(const FRPGRidingRepState& State);
    void WatchExecution();
    TWeakObjectPtr<URPGRidingComponent> Riding;
    FRPGRidingEmoteTicket Ticket;
    FDelegateHandle ChangedHandle;
    FTimerHandle WatchTimer;
    bool bStarting=false;
};
