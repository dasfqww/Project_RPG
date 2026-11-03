#pragma once

#include "Ability/RPGBossGameplayAbility.h"
#include "TimerManager.h"
#include "RPGBossTelegraphedStrikeAbility.generated.h"

class UAnimMontage;

/** A reusable targeted strike: lock the server aim point, publish a windup, then query once. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGBossTelegraphedStrikeAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	/** Stable presentation identifier; the client may map it to an indicator or cue. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike")
	FGameplayTag ActionTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike", meta = (ClampMin = "0.01", ClampMax = "120.0", Units = "s"))
	float WindupSeconds = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike", meta = (ClampMin = "1.0", Units = "cm"))
	float Radius = 200.f;
	/** Opt-in data shape. Existing assets retain their authored sphere Radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Shape")
	bool bUseCustomHitShape = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Shape", meta = (EditCondition = "bUseCustomHitShape"))
	FRPGHitQueryShape HitShape;
	/** Source-centred slashes/cones versus target-centred impacts; aim yaw is locked at activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Shape")
	bool bCenterHitShapeOnSource = false;

	/** Opt-in area impact. All eligible threat participants inside the locked warning can be hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike")
	bool bHitAllThreatParticipants = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike")
	FScalableFloat BaseDamage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike")
	FRPGSkillSecurityProfile SecurityProfile;

	/** Cosmetic-only, in-place montage; hit timing remains WindupSeconds on the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Presentation")
	TObjectPtr<UAnimMontage> PresentationMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Presentation", meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float PresentationPlayRate = 1.f;

	/** One-shot warning, not a persistent gameplay state or a damage event. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Presentation", meta = (Categories = "GameplayCue"))
	FGameplayTag WindupCueTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Strike|Presentation", meta = (ClampMin = "0.0", ClampMax = "5.0", Units = "s"))
	float RecoverySeconds = 0.f;

protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	bool BuildStrikeHitProfile(FRPGHitQueryProfile& Out) const;
	FRPGHitQueryProfile LockedHitProfile;
	FTransform LockedQueryTransform = FTransform::Identity;
	FTimerHandle StrikeTimer;
	FVector LockedAimLocation = FVector::ZeroVector;
	int32 LockedTelegraphSequence = 0;
	int32 LockedEncounterId = 0;
	TWeakObjectPtr<UAnimMontage> ExecutionMontage;
	void StopStrikePresentation();
	void ResolveStrike(uint64 StartedSerial);
};
