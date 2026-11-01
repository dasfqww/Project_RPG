#pragma once

#include "Ability/RPGBossGameplayAbility.h"
#include "AI/BTTask_RPGBossAbility.h"
#include "RPGBossAbilityTestFixture.generated.h"

class AAIController;
class UBehaviorTreeComponent;

UCLASS(Transient, NotBlueprintable)
class URPGBossAbilityTestProbe : public UObject
{
	GENERATED_BODY()
public:
	int32 Activated = 0;
	int32 Ended = 0;
	int32 Cancelled = 0;
	int32 FinishedTasks = 0;
	EBTNodeResult::Type LastResult = EBTNodeResult::InProgress;
	bool bEndImmediately = false;
	bool bCancelImmediately = false;
	bool bBlockActivation = false;
	bool bResetOnCanActivate = false;
	bool bCommitOnActivate = false;
	TWeakObjectPtr<APawn> SeenTarget;
};

UCLASS(Transient, NotBlueprintable)
class URPGBossTestAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	void FinishTest(bool bCancel);
protected:
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};

/** Uses GAS CheckCooldown against an ASC tag; no separate selector cooldown model. */
UCLASS(Transient, NotBlueprintable)
class URPGBossCooldownPatternTestAbility : public URPGBossTestAbility
{
	GENERATED_BODY()
public:
	URPGBossCooldownPatternTestAbility();
	virtual const FGameplayTagContainer* GetCooldownTags() const override { return &PatternCooldownTags; }
private:
	FGameplayTagContainer PatternCooldownTags;
};

/** Exercises the production boss cooldown implementation, not loose-tag emulation. */
UCLASS(Transient, NotBlueprintable)
class URPGBossTimedCooldownPatternTestAbility : public URPGBossTestAbility
{
	GENERATED_BODY()
public:
	URPGBossTimedCooldownPatternTestAbility();
};

UCLASS(Transient, NotBlueprintable)
class UBTTask_RPGBossTestAbility : public UBTTask_RPGBossAbility
{
	GENERATED_BODY()
public:
	UPROPERTY()
	TObjectPtr<URPGBossAbilityTestProbe> Probe;
protected:
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;
};

/** Creates a one-shot native tree; no content assets, no editor graph generation. */
UBehaviorTreeComponent* StartBossAbilityTestTree(AAIController* Controller, URPGBossAbilityTestProbe* Probe,
	float Timeout = 120.f, TSubclassOf<URPGBossGameplayAbility> AbilityClass = nullptr, bool bUsePhasePatterns = false);
