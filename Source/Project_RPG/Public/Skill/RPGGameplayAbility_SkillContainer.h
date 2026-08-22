// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "RPGGameplayAbility_SkillContainer.generated.h"

class URPGSkillDefinition;
class URPGSkillAction;
class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitGameplayEvent;
class UAbilityTask_WaitInputPress;
class UAbilityTask_WaitInputRelease;
class UGameplayEffect;
class UNiagaraComponent;
struct FRPGHitQueryResult;

/**
 * URPGGameplayAbility_SkillContainer
 * 
 * 모든 스킬이 공통으로 사용하는 GAS Ability입니다.
 * SkillDefinition을 읽어서 적절한 SkillAction을 실행시키는 "컨테이너" 역할을 합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility_SkillContainer
	: public URPGPlayerGameplayAbility
	, public IRPGSkillExecutionHost
	, public IRPGSkillTargetingHost
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_SkillContainer();

	//~ Begin UGameplayAbility Interface
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	virtual void CancelAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateCancel) override;
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	//~ End UGameplayAbility Interface

	virtual bool PreReplicateAbilityInputPressed() override;
	virtual bool PreReplicateAbilityInputReleased() override;

	// Action에서 작업이 끝났을 때 호출
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void OnActionEnded();

	// Action을 위한 UI/기능 래퍼 (Public)
	void ShowProgressBar_Internal() { ShowProgressBar(GetCurrentActorInfo()); }
	void HiddenProgressBar_Internal() { HiddenProgressBar(GetCurrentActorInfo()); }
	void UpdateProgressBar_Internal(FString TimeText, float Current, float Max) { ShowProcessBarFilling(TimeText, Current, Max); }
	void ProgressCompleted_Internal() { ProgessCompleted(); }
	void ExecuteWaitEvent_Internal() { ExecWaitGameplayEvent(); }
	const FRPGSkillRuntimeSpec& GetActiveSkillSpec() const { return ActiveSkillSpec; }

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Targeting")
	FRPGSkillTargetResult GetActiveSkillTargetResult() const
	{
		return ActiveTargetResult;
	}

	/** Runs the authored hit profile at the target policy's resolved transform. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|Targeting")
	bool ExecuteActiveSkillHitQuery(
		TArray<FRPGHitQueryResult>& OutResults) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Security")
	FRPGSkillSecurityProfile GetActiveSkillSecurityProfile() const
	{
		return ActiveSkillSpec.SecurityProfile;
	}

	/**
	 * Safe BP path: server HitQuery, profile validation, and GameplayEffect
	 * application are executed as one indivisible authored operation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|Skill|Security",
		meta = (DisplayName = "Execute Authorized Skill Damage",
			ExpandBoolAsExecs = "ReturnValue"))
	bool ExecuteAuthorizedSkillDamage(
		TSubclassOf<UGameplayEffect> AuthorizedDamageEffectClass,
		float BaseDamage,
		FGameplayTag SetByCallerDamageTag,
		TArray<FRPGHitQueryResult>& OutAppliedHits,
		int32& OutAppliedTargetCount,
		FText& OutError);

	//~ Begin IRPGSkillExecutionHost
	virtual UWorld* GetSkillExecutionWorld() const override;
	virtual const FRPGSkillRuntimeSpec& GetSkillRuntimeSpec() const override;
	virtual const FRPGSkillTargetResult& GetSkillTargetResult() const override;
	virtual bool RefreshSkillTarget() override;
	virtual bool IsSkillInputPressed() const override;
	virtual bool PlaySkillMontage(FName StartSection) override;
	virtual bool JumpToSkillMontageSection(FName SectionName) override;
	virtual void FinishSkillExecution(bool bWasCancelled) override;
	virtual void ShowSkillProgress() override;
	virtual void HideSkillProgress() override;
	virtual void UpdateSkillProgress(float Current, float Maximum) override;
	virtual void NotifySkillProgressCompleted() override;
	virtual void StartSkillPersistentVFX() override;
	virtual void StopSkillPersistentVFX() override;
	//~ End IRPGSkillExecutionHost

	//~ Begin IRPGSkillTargetingHost
	virtual UWorld* GetSkillTargetingWorld() const override;
	virtual AActor* GetSkillSourceActor() const override;
	virtual bool GetSkillCameraAimRay(
		FVector& OutOrigin,
		FVector& OutDirection) const override;
	virtual AActor* GetSkillLockedTarget() const override;
	virtual const FRPGHitQueryFilter& GetSkillTargetValidationFilter() const override;
	//~ End IRPGSkillTargetingHost

protected:
	virtual float GetRPGCooldownDuration(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo) const override;
	virtual const UObject* GetRPGCooldownSourceObject() const override;

	UFUNCTION()
	void HandlePolicyMontageCompleted();

	UFUNCTION()
	void HandlePolicyMontageInterrupted();

	UFUNCTION()
	void HandlePolicyExecutionEvent(FGameplayEventData Payload);

	UFUNCTION()
	void HandleReplicatedInputPressed(float TimeWaited);

	UFUNCTION()
	void HandleReplicatedInputReleased(float TimeHeld);

	void HandleReplicatedTargetData(
		const FGameplayAbilityTargetDataHandle& TargetData,
		FGameplayTag ApplicationTag);
	void HandleInitialTargetDataTimeout();

	bool StartPolicyEventTask();
	void StartExecutionInputTasks();
	void ArmInputPressedTask();
	void ArmInputReleasedTask();
	void DispatchExecutionInputPressed();
	void DispatchExecutionInputReleased();
	bool InitializeTargetingPolicy();
	bool StartSkillAfterTargetReady();
	bool WaitForInitialReplicatedTargetData();
	bool ShouldWaitForRemoteTargetData() const;
	bool RefreshSkillTargetWithApplicationTag(FGameplayTag ApplicationTag);
	bool SendActiveTargetDataToServer(FGameplayTag ApplicationTag);
	void TryDispatchRemotePolicyExecutionEvent();
	void ResetRemotePolicyEventSync();
	bool ValidateLegacyReplicatedTarget(
		const FRPGSkillTargetResult& SubmittedResult,
		FRPGSkillTargetResult& OutValidatedResult,
		FText& OutError) const;
	void RemoveReplicatedTargetDataDelegate();
	bool ResolveLegacyTarget();
	void ApplyResolvedAimRotation();

	// 이 어빌리티가 어떤 스킬인지 정의 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RPG|Skill")
	TObjectPtr<URPGSkillDefinition> SkillDefinition;

	// 현재 실행 중인 액션 인스턴스
	UPROPERTY()
	TObjectPtr<URPGSkillAction> ActiveAction;

	UPROPERTY(Transient)
	TObjectPtr<URPGSkillExecutionPolicy> ActiveExecutionPolicy;

	UPROPERTY(Transient)
	TObjectPtr<URPGSkillTargetingPolicy> ActiveTargetingPolicy;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_PlayMontageAndWait> ActiveMontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitGameplayEvent> ActivePolicyEventTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputPress> ActiveInputPressedTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitInputRelease> ActiveInputReleasedTask;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> PersistentSkillVFX;

	/** Frozen at activation so client/server execution consumes one resolved input. */
	UPROPERTY(Transient)
	FRPGSkillRuntimeSpec ActiveSkillSpec;

	UPROPERTY(Transient)
	FRPGSkillTargetResult ActiveTargetResult;

	/** Prevents a malicious or disconnected client from pinning a server ability. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill|Network",
		meta = (ClampMin = "0.1", Units = "s"))
	float InitialTargetDataTimeout = 2.0f;

	/** Maximum client/server montage-notify skew accepted for one policy event. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill|Network",
		meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float RemotePolicyEventSyncTolerance = 0.35f;

	bool bSkillInputPressed = false;
	bool bWaitingForInitialTargetData = false;
	bool bPreparedTargetForInputReplication = false;
	bool bRemotePolicyEventWindowOpen = false;
	bool bRemotePolicyEventDataReady = false;
	int32 ServerAppliedHitCount = 0;
	double RemotePolicyEventWindowTime = 0.0;
	double RemotePolicyEventDataTime = 0.0;
	FDelegateHandle ReplicatedTargetDataDelegateHandle;
	FTimerHandle InitialTargetDataTimeoutHandle;
};
