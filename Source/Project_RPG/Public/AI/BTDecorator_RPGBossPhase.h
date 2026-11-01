#pragma once

#include "BehaviorTree/BTDecorator.h"
#include "AI/RPGBossAIComponent.h"
#include "GameplayTagContainer.h"
#include "BTDecorator_RPGBossPhase.generated.h"

/** Server-owned phase condition. No Blackboard key or replicated decision state. */
UCLASS()
class PROJECT_RPG_API UBTDecorator_RPGBossPhase : public UBTDecorator
{
	GENERATED_BODY()
public:
	UBTDecorator_RPGBossPhase();

	/** Empty fails the condition. The boss must also be InCombat. */
	UPROPERTY(EditAnywhere, Category = "Boss|Phase")
	FGameplayTag RequiredPhase;

	/** Disable to match a parent tag such as Boss.Phase.Mechanic. */
	UPROPERTY(EditAnywhere, Category = "Boss|Phase")
	bool bExactMatch = true;

	virtual FString GetStaticDescription() const override;
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp) override;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	TWeakObjectPtr<UBehaviorTreeComponent> ObservedTree;
	TWeakObjectPtr<URPGBossAIComponent> ObservedBoss;
	void StopObserving();

	UFUNCTION()
	void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
};
