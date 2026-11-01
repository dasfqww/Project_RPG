#pragma once

#include "BehaviorTree/BTTaskNode.h"
#include "GameplayAbilitySpecHandle.h"
#include "BTTask_RPGBossAbility.generated.h"

class URPGBossGameplayAbility;
class UAbilitySystemComponent;
class APawn;
struct FAbilityEndedData;

/** Runs one already-granted boss ability. Per-tree instance; never grants abilities or sends client RPCs. */
UCLASS()
class PROJECT_RPG_API UBTTask_RPGBossAbility : public UBTTaskNode
{
	GENERATED_BODY()
public:
	UBTTask_RPGBossAbility();
	UPROPERTY(EditAnywhere, Category = "Boss")
	TSubclassOf<URPGBossGameplayAbility> AbilityClass;

	/** Opt-in: ignore AbilityClass and use the current frozen phase's pattern selection.
	 * GAS checks tags/costs/cooldowns; this task never grants missing abilities. */
	UPROPERTY(EditAnywhere, Category = "Boss")
	bool bUsePhasePatterns = false;

	/** Mandatory finite timeout; cancellation only affects the execution started by this node. */
	UPROPERTY(EditAnywhere, Category = "Boss", meta = (ClampMin = "0.01", ClampMax = "600.0"))
	float TimeoutSeconds = 15.0f;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp) override;

protected:
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

private:
	TWeakObjectPtr<UAbilitySystemComponent> ASC;
	TWeakObjectPtr<URPGBossGameplayAbility> Ability;
	FGameplayAbilitySpecHandle Handle;
	FDelegateHandle EndedDelegate;
	uint64 ExecutionSerial = 0;
	double Deadline = 0.0;
	bool bOwnedExecution = false;
	bool bEnded = false;
	bool bCancelled = false;
	void OnAbilityEnded(const FAbilityEndedData& Data);
	void Cleanup();
	FGameplayAbilitySpecHandle SelectAbilityHandle(APawn* Pawn) const;
};
