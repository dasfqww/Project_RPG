#pragma once

#include "BehaviorTree/BTService.h"
#include "BTService_RPGBossState.generated.h"

/** Server-only snapshot mirror. Place on the root branch so Idle/Resetting still update. */
UCLASS()
class PROJECT_RPG_API UBTService_RPGBossState : public UBTService
{
	GENERATED_BODY()
public:
	UBTService_RPGBossState();
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Boss|Blackboard")
	FBlackboardKeySelector InCombatKey;

	UPROPERTY(EditAnywhere, Category = "Boss|Blackboard")
	FBlackboardKeySelector PhaseIndexKey;

	UPROPERTY(EditAnywhere, Category = "Boss|Blackboard")
	FBlackboardKeySelector EnragedKey;
};
