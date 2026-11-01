#pragma once

#include "Abilities/GameplayAbility.h"
#include "RPGDungeonGameplayRevocationTestFixture.generated.h"

/** Native, untagged GAS execution: proves revocation is not limited to RPG/BP tags. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonRevocationTestAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	URPGDungeonRevocationTestAbility();
protected:
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
