#pragma once

#include "Ability/RPGBossGameplayAbility.h"
#include "TimerManager.h"
#include "RPGBossPersistentHazardAbility.generated.h"

class ARPGBossPersistentHazard;

/** Cast at a locked aim point, then release an independent encounter-owned persistent area. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGBossPersistentHazardAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	URPGBossPersistentHazardAbility();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard") FGameplayTag ActionTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard", meta = (ClampMin = "0.01", ClampMax = "120")) float WindupSeconds = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard", meta = (ClampMin = "1", ClampMax = "2500")) float Radius = 200.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard", meta = (ClampMin = "0.1", ClampMax = "120")) float DurationSeconds = 5.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard", meta = (ClampMin = "0.1", ClampMax = "30")) float PulseInterval = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard", meta = (ClampMin = "1", ClampMax = "16")) int32 MaximumActiveHazards = 4;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard") TSubclassOf<ARPGBossPersistentHazard> HazardClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard") TSubclassOf<UGameplayEffect> DamageEffectClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard") FScalableFloat BaseDamage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Hazard") FRPGSkillSecurityProfile SecurityProfile;
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
	FTimerHandle CastTimer;
	FVector LockedCenter = FVector::ZeroVector;
	int32 LockedSequence = 0;
	TWeakObjectPtr<ARPGBossPersistentHazard> PendingHazard;
	bool IsTargetInRange(const APawn* Source, const APawn* Target) const;
	bool HasHazardCapacity(const APawn* Source) const;
	void ReleaseHazard(uint64 StartedSerial);
};
