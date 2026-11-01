#pragma once

#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGGameplayAbility_WeaponStance.generated.h"

/** Native stance transition; existing graphs only run for non-item startup weapons. */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_WeaponStance : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGGameplayAbility_WeaponStance();
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
		const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	UPROPERTY(EditDefaultsOnly, Category = "Weapon Stance")
	EEquipState DesiredWeaponState = EEquipState::WeaponSet_Primary;
};
