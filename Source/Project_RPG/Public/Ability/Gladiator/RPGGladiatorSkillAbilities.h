#pragma once

#include "Ability/Gladiator/RPGGameplayAbility_Equipment.h"
#include "Ability/Gladiator/RPGGameplayAbility_Weapon_Melee.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Engine/EngineTypes.h"
#include "RPGGladiatorSkillAbilities.generated.h"

class AGameplayAbilityTargetActor;
class AGameplayAbilityTargetActor_GroundTrace;
class AGameplayAbilityWorldReticle;
class ARPGGameplayAbilityTargetActor_LineTraceHighlight;
class UAnimMontage;
class UGameplayEffect;
class UInputAction;
class UNiagaraSystem;
class URPGCameraMode;

/** Common compatibility implementation for the four class buff abilities. */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_Buff : public URPGGameplayAbility_Equipment
{
	GENERATED_BODY()

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void ApplyEffect();

	UFUNCTION(BlueprintNativeEvent)
	void ApplyAdditionalEffects();

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Buff")
	TObjectPtr<UAnimMontage> BuffMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Buff")
	TSubclassOf<UGameplayEffect> BuffGameplayEffectClass;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Buff")
	TObjectPtr<UNiagaraSystem> BuffEffect;
};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_ShieldBash : public URPGGameplayAbility_Weapon_Melee
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_ShieldBash(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void OnShieldBashBegin(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Shield Bash")
	TObjectPtr<UAnimMontage> ShieldBashMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Shield Bash")
	float Damage = 50.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Shield Bash")
	float StunDuration = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Shield Bash")
	float Distance = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Shield Bash")
	float RadiusMultiplier = 3.25f;
};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_GroundBreaker : public URPGGameplayAbility_Weapon_Melee
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_GroundBreaker(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void OnGroundBreakerBegin(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Ground Breaker")
	TObjectPtr<UAnimMontage> GroundBreakerMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Ground Breaker")
	float DistanceOffset = 150.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Ground Breaker")
	float Damage = 80.0f;

	/** Historical spelling is preserved for serialized assets. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Ground Breaker")
	float StunDruation = 3.0f;
};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_WhirlwindSlash : public URPGGameplayAbility_Weapon_Melee
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_WhirlwindSlash(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void OnTrace(FGameplayEventData Payload);

	UFUNCTION()
	void OnReset(FGameplayEventData Payload);

	UFUNCTION()
	void OnWhirlwindSlashBegin(FGameplayEventData Payload);

	UFUNCTION()
	void OnWhirlwindSlashEnd(FGameplayEventData Payload);

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Whirlwind Slash")
	TObjectPtr<UAnimMontage> WhirlwindSlashMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Whirlwind Slash")
	float Damage = 10.0f;

};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_PiercingShot : public URPGGameplayAbility_Equipment
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_PiercingShot(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION()
	void OnPiercingShotBegin(FGameplayEventData Payload);

	UFUNCTION()
	void OnInputConfirm();

	UFUNCTION()
	void OnInputCancel();

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TObjectPtr<UInputAction> MainHandInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TObjectPtr<UInputAction> OffHandInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TObjectPtr<UAnimMontage> ADSStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TObjectPtr<UAnimMontage> ADSEndMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TObjectPtr<UAnimMontage> ReleaseMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TSubclassOf<AActor> ProjectileClass;

	/** D1 bow-projectile properties retained on this flattened compatibility parent. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	FName SpawnSocketName;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	bool bApplyAimAssist = true;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot", meta = (EditCondition = "bApplyAimAssist"))
	float AimAssistMinDistance = 100.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot", meta = (EditCondition = "bApplyAimAssist"))
	float AimAssistMaxDistance = 10000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Piercing Shot")
	TSubclassOf<URPGCameraMode> ADSCameraModeClass;
};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_Targeting : public URPGGameplayAbility_Equipment
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_Targeting(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Targeting")
	void ConfirmSkill();

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Targeting")
	void CancelSkill();

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Targeting")
	void ResetSkill();

	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|Gladiator|Targeting")
	void WaitTargetData();

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TObjectPtr<UAnimMontage> CastStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TObjectPtr<UAnimMontage> CastEndMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TObjectPtr<UAnimMontage> SpellMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting", meta = (Categories = "GameplayCue"))
	FGameplayTag CastGameplayCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting", meta = (Categories = "GameplayCue"))
	FGameplayTag BurstGameplayCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TArray<TSubclassOf<UGameplayEffect>> GameplayEffectClasses;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TObjectPtr<UInputAction> MainHandInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Targeting")
	TObjectPtr<UInputAction> OffHandInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|Targeting")
	TSubclassOf<ARPGGameplayAbilityTargetActor_LineTraceHighlight> TargetActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|Targeting")
	TSubclassOf<AGameplayAbilityWorldReticle> TargetingReticleClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|Targeting")
	float MaxRange = 1000.0f;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	FGameplayAbilityTargetDataHandle TargetDataHandle;
};

UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_Skill_AOE : public URPGGameplayAbility_Equipment
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Skill_AOE(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|AOE")
	void ConfirmSkill();

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|AOE")
	void CancelSkill();

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|AOE")
	void ResetSkill();

	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|Gladiator|AOE")
	void WaitTargetData();

	UFUNCTION()
	void OnMontageFinished();

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TObjectPtr<UAnimMontage> CastStartMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TObjectPtr<UAnimMontage> CastEndMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TObjectPtr<UAnimMontage> SpellMontage;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE", meta = (Categories = "GameplayCue"))
	FGameplayTag CastGameplayCueTag;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TSubclassOf<AActor> AOESpawnerClass;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TObjectPtr<UInputAction> MainHandInputAction;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|AOE")
	TObjectPtr<UInputAction> OffHandInputAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	TSubclassOf<AGameplayAbilityTargetActor_GroundTrace> TargetActorClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	TSubclassOf<AGameplayAbilityWorldReticle> AOEReticleClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	float CollisionRadius = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	float CollisionHeight = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	float MaxRange = 1000.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	float AcceptanceMultiplier = 1.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Gladiator|AOE")
	FCollisionProfileName TraceProfile;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	FGameplayAbilityTargetDataHandle TargetDataHandle;
};
