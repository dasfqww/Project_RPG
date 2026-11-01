// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGComboSkillAbility.generated.h"

class UAbilityTask_WaitInputPress;
class UAbilityTask_WaitInputRelease;
class URPGBossThreatComponent;
class AController;
class UAnimMontage;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGComboSkillAbility : public URPGPlayerGameplayAbility
{
	GENERATED_BODY()
public:
	URPGComboSkillAbility();

	//~ Begin UGameplayAbility Interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	//~ End UGameplayAbility Interface

	virtual void PlaySkillMontage() override;

	UFUNCTION()
	void OnComboMontageCompleted();

	UFUNCTION()
	void OnComboInputPressed(float TimeWaited);

	UFUNCTION()
	void OnComboInputReleased(float TimeHeld);

	//virtual void HandleApplyDamage(const FGameplayEventData& InGameplayEventData);

	void ResetComboCount();

protected:
	virtual bool ApplyLegacyDamageToServerHit(const FHitResult& ServerHit,
		const FGameplayEffectSpecHandle& DamageSpec) override;

private:
	friend class FRPGBossPlayerAttackTest;
	void CaptureBossEncounters(APawn* Source);
	int32 GetCapturedBossEncounter(APawn* Source, URPGBossThreatComponent* Threat) const;
	TMap<TWeakObjectPtr<URPGBossThreatComponent>, int32> BossAttackEncounters;
	TWeakObjectPtr<APawn> BossAttackSource;
	TWeakObjectPtr<AController> BossAttackController;
	void WaitForNextInputTransition();
	void RecordResolveComboHit(int32 Step, uint64 Serial, bool bApplied);
	TSet<int32> ResolveComboHitSteps;
	TWeakObjectPtr<UAnimMontage> ResolveComboMontage;
	uint64 ResolveDamageSerial = 0;
	int32 ResolveCurrentStep = INDEX_NONE, ResolveStepCount = 0;
	bool bResolveComboAwarded = false;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FMultipleSectionData DefaultAttackSectionData;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	FMultipleSectionData DefaultAttackSectionData_Rage;

	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<UGameplayEffect> GainIdentityEffect;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	int32 InitComboCount = 1;
	
	UPROPERTY(EditDefaultsOnly, Category = "Skill", meta = (AllowPrivateAccess = "true"))
	float TimeToTimerReset = 0.5f;

	int32 CurrentComboCount;

	int32 UsedComboCount = 0;

	bool bComboInputHeld = false;
	bool bAdvanceBuffered = false;
	bool bHasNextComboSection = false;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputPress> WaitInputPressTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> WaitInputReleaseTask;

	FTimerHandle ComboCountResetTimerHandle;
};
