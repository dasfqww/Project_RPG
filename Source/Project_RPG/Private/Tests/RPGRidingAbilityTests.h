#pragma once
#include "Abilities/GameplayAbility.h"
#include "Riding/RPGRidingAbilityPolicy.h"
#include "RPGRidingAbilityTests.generated.h"

/** Minimal GAS fixture: exercises the shared native policy, not the full game ability implementation. */
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateTestAbility : public UGameplayAbility, public IRPGRidingAbilityPolicy
{
    GENERATED_BODY()
public:
    URPGRidingGateTestAbility();
    virtual bool IsAutomaticRidingExempt() const override { return false; }
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Failures) const override;
    virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
    void Stop();
    int32 Activations = 0;
};

UCLASS(Transient, NotBlueprintable)
class URPGRidingGateAutomaticAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: virtual bool IsAutomaticRidingExempt() const override { return true; } };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateActionAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: virtual bool IsRidingActionAbility() const override { return true; } };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateLocalAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateLocalAbility(); };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateAttackAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateAttackAbility(); };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateDeathAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateDeathAbility(); };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateLockAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateLockAbility(); };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateMixedAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateMixedAbility(); };
UCLASS(Transient, NotBlueprintable)
class URPGRidingGateReactionAbility : public URPGRidingGateTestAbility
{ GENERATED_BODY() public: URPGRidingGateReactionAbility(); };
