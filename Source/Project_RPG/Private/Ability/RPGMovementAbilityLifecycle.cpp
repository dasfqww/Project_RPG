#include "Ability/RPGGameplayAbility.h"

void URPGGameplayAbility::EndInterruptedMovementAbility()
{
	if (!IsInstantiated() || !IsActive())
	{
		return;
	}
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid() ||
		(!ActorInfo->IsNetAuthority() && !ActorInfo->IsLocallyControlled()))
	{
		return;
	}
	// Montage callbacks can run inside CancelAbility's broadcast. Calling
	// CancelAbility again there recurses. EndAbility retains the cancelled
	// result, and GAS destroys/revokes all tasks belonging to this ability.
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
}
