#pragma once

#include "Abilities/GameplayAbility.h"
#include "RPGMovementAuthorizationTestAbility.generated.h"

/** Keeps real BP-facing AbilityTasks alive in the opt-in network fixture. */
UCLASS(Transient, NotBlueprintable)
class URPGMovementAuthorizationTestAbility final : public UGameplayAbility
{
	GENERATED_BODY()
public:
	URPGMovementAuthorizationTestAbility();
	void FinishProbeAbility();
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
};
