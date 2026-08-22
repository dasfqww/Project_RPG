#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "RPGSkillExecutionPolicy.generated.h"

struct FGameplayEventData;
struct FInstancedStruct;
struct FRPGSkillRuntimeSpec;
struct FRPGSkillTargetResult;

/**
 * Narrow service surface exposed by the owning Gameplay Ability.
 * Policies decide execution state; the host owns GAS tasks and ability lifetime.
 */
class PROJECT_RPG_API IRPGSkillExecutionHost
{
public:
	virtual ~IRPGSkillExecutionHost() = default;

	virtual UWorld* GetSkillExecutionWorld() const = 0;
	virtual const FRPGSkillRuntimeSpec& GetSkillRuntimeSpec() const = 0;
	virtual const FRPGSkillTargetResult& GetSkillTargetResult() const = 0;
	virtual bool RefreshSkillTarget() = 0;
	virtual bool IsSkillInputPressed() const = 0;
	virtual bool PlaySkillMontage(FName StartSection) = 0;
	virtual bool JumpToSkillMontageSection(FName SectionName) = 0;
	virtual void FinishSkillExecution(bool bWasCancelled) = 0;
	virtual void ShowSkillProgress() = 0;
	virtual void HideSkillProgress() = 0;
	virtual void UpdateSkillProgress(float Current, float Maximum) = 0;
	virtual void NotifySkillProgressCompleted() = 0;
	virtual void StartSkillPersistentVFX() = 0;
	virtual void StopSkillPersistentVFX() = 0;
};

/**
 * One activation-local execution strategy.
 * Input semantics live here rather than in InputTags or GameplayAbility subclasses.
 */
UCLASS(Abstract)
class PROJECT_RPG_API URPGSkillExecutionPolicy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(IRPGSkillExecutionHost& InHost);

	/** Returns false when authored runtime data cannot start this policy. */
	virtual bool StartExecution();
	virtual bool ValidateExecutionConfig(
		const FInstancedStruct& Config,
		FText& OutError) const;
	/** Validates activation-local montage and config references before CommitAbility. */
	virtual bool ValidateRuntimeSpec(FText& OutError) const;
	virtual void OnInputPressed();
	virtual void OnInputReleased();
	/** Optional Gameplay Event observed through an ability-owned GAS task. */
	virtual FGameplayTag GetExecutionEventTag() const;
	virtual void OnExecutionEvent(const FGameplayEventData& Payload);
	virtual void OnMontageCompleted();
	virtual void OnMontageInterrupted();
	virtual void EndExecution();
	virtual void CancelExecution();

protected:
	const FRPGSkillRuntimeSpec& GetRuntimeSpec() const;
	IRPGSkillExecutionHost* GetHost() const { return Host; }
	UWorld* GetWorld() const override;

private:
	IRPGSkillExecutionHost* Host = nullptr;
};

UCLASS()
class PROJECT_RPG_API URPGSkillExecutionPolicy_Instant
	: public URPGSkillExecutionPolicy
{
	GENERATED_BODY()

public:
	virtual bool StartExecution() override;
	virtual bool ValidateExecutionConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ValidateRuntimeSpec(FText& OutError) const override;
};

UCLASS()
class PROJECT_RPG_API URPGSkillExecutionPolicy_Charge
	: public URPGSkillExecutionPolicy
{
	GENERATED_BODY()

public:
	virtual bool StartExecution() override;
	virtual bool ValidateExecutionConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ValidateRuntimeSpec(FText& OutError) const override;
	virtual void OnInputReleased() override;
	virtual void EndExecution() override;
	virtual void CancelExecution() override;

	int32 GetCurrentChargeLevel() const { return CurrentChargeLevel; }

private:
	void UpdateCharge();
	void ReleaseCharge();
	float GetChargeTimePerLevel() const;
	const struct FRPGSkillChargeExecutionConfig* GetChargeConfig() const;

	FTimerHandle ChargeUpdateTimerHandle;
	float ChargeStartTime = 0.0f;
	int32 CurrentChargeLevel = 0;
	bool bReachedMaximumCharge = false;
	bool bReleased = false;
};

UCLASS()
class PROJECT_RPG_API URPGSkillExecutionPolicy_Holding
	: public URPGSkillExecutionPolicy
{
	GENERATED_BODY()

public:
	virtual bool StartExecution() override;
	virtual bool ValidateExecutionConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ValidateRuntimeSpec(FText& OutError) const override;
	virtual void OnInputReleased() override;
	virtual void OnMontageCompleted() override;
	virtual void EndExecution() override;
	virtual void CancelExecution() override;

private:
	void UpdateHolding();
	void CompleteHolding(bool bSuccessful);
	void CleanupHolding();
	float GetScaledTime(float AuthoredTime) const;
	const struct FRPGSkillHoldingExecutionConfig* GetHoldingConfig() const;

	FTimerHandle HoldingUpdateTimerHandle;
	float HoldingStartTime = 0.0f;
	bool bPerfectZoneReached = false;
	bool bResolved = false;
	bool bFinishAsCancelled = false;
};

UCLASS()
class PROJECT_RPG_API URPGSkillExecutionPolicy_Combo
	: public URPGSkillExecutionPolicy
{
	GENERATED_BODY()

public:
	virtual bool StartExecution() override;
	virtual bool ValidateExecutionConfig(
		const FInstancedStruct& Config,
		FText& OutError) const override;
	virtual bool ValidateRuntimeSpec(FText& OutError) const override;
	virtual void OnInputPressed() override;
	virtual FGameplayTag GetExecutionEventTag() const override;
	virtual void OnExecutionEvent(const FGameplayEventData& Payload) override;

	int32 GetCurrentComboIndex() const { return CurrentComboIndex; }

private:
	const struct FRPGSkillComboExecutionConfig* GetComboConfig() const;

	int32 CurrentComboIndex = INDEX_NONE;
	bool bAdvanceBuffered = false;
};
