#pragma once

#include "Animation/AnimInstance.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "RPGSwordWaveAbility.generated.h"

class ARPGSwordWaveProjectile;

/** Instant skill with one server projectile launched by the active montage's release notify. */
UCLASS()
class PROJECT_RPG_API URPGSwordWaveAbility : public URPGGameplayAbility_SkillContainer
{
	GENERATED_BODY()

public:
	URPGSwordWaveAbility();
	virtual bool PlaySkillMontage(FName StartSection) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

protected:
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill")
	TSubclassOf<ARPGSwordWaveProjectile> ProjectileClass;

private:
	friend class ARPGSwordWaveProjectile;

	UFUNCTION()
	void HandleSwordWaveRelease(FName NotifyName, const FBranchingPointNotifyPayload& Payload);
	bool ApplySwordWaveHit(const ARPGSwordWaveProjectile& Projectile, APawn* Target, const FHitResult& Hit);

	TWeakObjectPtr<UAnimInstance> ReleaseAnimInstance;
	TWeakObjectPtr<ARPGSwordWaveProjectile> ActiveProjectile;
	int32 ReleaseMontageInstanceId = INDEX_NONE;
	bool bLaunchAttempted = false;
};
