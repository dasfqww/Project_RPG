#pragma once

#include "Ability/RPGBossGameplayAbility.h"
#include "TimerManager.h"
#include "RPGBossDashAbility.generated.h"

class ACharacter;
class UAbilityTask_ApplyRootMotionConstantForce;
class URPGBossDashComponent;

/** Server-only, fixed-direction dash. Sweep actual traveled segments; once per participant per cast. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGBossDashAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash") FGameplayTag ActionTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.01", ClampMax = "120")) float WindupSeconds = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1", ClampMax = "3000")) float MaximumDistance = 800.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "0.05", ClampMax = "5")) float DashDuration = .5f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash", meta = (ClampMin = "1", ClampMax = "500")) float HitRadius = 100.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash") TSubclassOf<UGameplayEffect> DamageEffectClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash") FScalableFloat BaseDamage;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Dash") FRPGSkillSecurityProfile SecurityProfile;
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
	UPROPERTY(Transient) TObjectPtr<UAbilityTask_ApplyRootMotionConstantForce> MotionTask;
	UPROPERTY(Transient) FGameplayEffectSpecHandle DamageTemplate;
	TWeakObjectPtr<ACharacter> SourceCharacter;
	TWeakObjectPtr<AController> SourceController;
	TWeakObjectPtr<APawn> AimTarget;
	TWeakObjectPtr<URPGBossDashComponent> DashState;
	TSet<TWeakObjectPtr<APawn>> HitParticipants;
	FRPGSkillSecurityProfile FrozenSecurity;
	FVector LockedAim = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
	float FrozenDuration = 0.f;
	float FrozenRadius = 0.f;
	float FrozenSpeed = 0.f;
	float FrozenDistanceLimit = 0.f;
	FName MotionName;
	int32 LockedSequence = 0;
	int32 LockedEncounter = 0;
	bool bDashing = false;
	bool bResolvingHits = false;
	FTimerHandle CastTimer;
	bool IsMovementReady(const ACharacter* Character) const;
	bool IsDashContextValid(uint64 Serial, bool bRequireAim) const;
	void ReleaseDash(uint64 Serial);
	void ResolveSegment(FVector Start, FVector End, uint64 Serial);
	UFUNCTION() void OnMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity);
	UFUNCTION() void OnMotionFinished();
	UFUNCTION() void OnControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
	UFUNCTION() void OnSourceDestroyed(AActor* Actor);
};
