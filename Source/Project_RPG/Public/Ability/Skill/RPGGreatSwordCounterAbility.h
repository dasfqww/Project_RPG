#pragma once

#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "RPGGreatSwordCounterAbility.generated.h"

/** A committed guard montage can intercept one frontal, blockable NPC weapon hit. */
UCLASS()
class PROJECT_RPG_API URPGGreatSwordCounterAbility : public URPGGameplayAbility_SkillContainer
{
	GENERATED_BODY()

public:
	bool TryInterceptWeaponHit(APawn& Attacker, APawn& Defender);

protected:
	virtual void HandleApplyAOEDamage(const FGameplayEventData& Payload) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	TWeakObjectPtr<APawn> CounterTarget;
	bool bIntercepted = false;
};
