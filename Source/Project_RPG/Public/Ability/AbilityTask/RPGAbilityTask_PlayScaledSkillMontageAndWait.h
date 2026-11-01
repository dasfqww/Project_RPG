#pragma once

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGAbilityTask_PlayScaledSkillMontageAndWait.generated.h"

class ACharacter;
class URPGGameplayAbility_SkillContainer;
class URPGSecurityValidationComponent;

/** Native GAS montage translation scaling with a cast-owned, bounded movement authorization. */
UCLASS()
class PROJECT_RPG_API URPGAbilityTask_PlayScaledSkillMontageAndWait : public UAbilityTask_PlayMontageAndWait
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|Movement",
		meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true"))
	static URPGAbilityTask_PlayScaledSkillMontageAndWait* PlayScaledSkillMontage(
		URPGGameplayAbility_SkillContainer* OwningAbility, UAnimMontage* Montage, float PlaybackRate, FName Section);
	virtual void Activate() override;
protected:
	virtual void OnDestroy(bool bInOwnerFinished) override;
private:
	void RejectMovement();
	FRPGSkillMovementSecurityProfile MovementProfile;
	TWeakObjectPtr<ACharacter> ExpectedAvatar;
	TWeakObjectPtr<URPGSecurityValidationComponent> AuthorizedSecurity;
	FGuid AuthorizationHandle;
	uint64 ExpectedActivationSerial = 0;
	bool bMayOwnTranslationScale = false;
};
