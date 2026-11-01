#pragma once

#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGBossLegacyAOETestAbility.generated.h"

/** Native compatibility fixture; never granted to production player content. */
UCLASS(Transient, NotBlueprintable)
class URPGBossLegacyAOETestAbility final : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGBossLegacyAOETestAbility();
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
		FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event) override;
	void Finish();
	void DispatchAOE();
};
