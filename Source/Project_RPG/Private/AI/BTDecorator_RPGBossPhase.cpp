#include "AI/BTDecorator_RPGBossPhase.h"

#include "AI/RPGBossAIComponent.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

UBTDecorator_RPGBossPhase::UBTDecorator_RPGBossPhase()
{
	NodeName = TEXT("RPG Boss Phase");
	bCreateNodeInstance = true;
	FlowAbortMode = EBTFlowAbortMode::Both;
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
}

bool UBTDecorator_RPGBossPhase::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Controller) || !Controller->HasAuthority() || !IsValid(Pawn) || !Pawn->HasAuthority() ||
		Pawn->IsActorBeingDestroyed() || !RequiredPhase.IsValid())
	{
		return false;
	}
	const auto* Boss = Pawn->FindComponentByClass<URPGBossAIComponent>();
	if (!IsValid(Boss)) { return false; }
	const FRPGBossState State = Boss->GetBossState();
	return State.EncounterState == ERPGBossEncounterState::InCombat &&
		(bExactMatch ? State.PhaseTag == RequiredPhase : State.PhaseTag.MatchesTag(RequiredPhase));
}

void UBTDecorator_RPGBossPhase::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);
	StopObserving();
	const AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Controller) || !Controller->HasAuthority() || !IsValid(Pawn) || !Pawn->HasAuthority() || Pawn->IsActorBeingDestroyed()) { return; }
	URPGBossAIComponent* Boss = Pawn->FindComponentByClass<URPGBossAIComponent>();
	if (!IsValid(Boss)) { return; }
	ObservedTree = &OwnerComp;
	ObservedBoss = Boss;
	Boss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged);
}

void UBTDecorator_RPGBossPhase::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current)
{
	UBehaviorTreeComponent* Tree = ObservedTree.Get();
	const AAIController* Controller = Tree ? Tree->GetAIOwner() : nullptr;
	const APawn* Pawn = IsValid(Controller) ? Controller->GetPawn() : nullptr;
	if (!IsValid(Tree) || !IsValid(Controller) || !Controller->HasAuthority() || !IsValid(Pawn) || !Pawn->HasAuthority() ||
		!ObservedBoss.IsValid() || Pawn->FindComponentByClass<URPGBossAIComponent>() != ObservedBoss.Get()) { return; }
	if (Previous.EncounterState == Current.EncounterState && Previous.PhaseTag == Current.PhaseTag) { return; }
	// Engine branch requests respect Observer Aborts and defer execution through BT.
	ConditionalFlowAbort(*Tree, EBTDecoratorAbortRequest::ConditionResultChanged);
}

void UBTDecorator_RPGBossPhase::StopObserving()
{
	if (URPGBossAIComponent* Boss = ObservedBoss.Get())
	{
		Boss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged);
	}
	ObservedBoss.Reset();
	ObservedTree.Reset();
}

void UBTDecorator_RPGBossPhase::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	StopObserving();
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);
}

void UBTDecorator_RPGBossPhase::OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp)
{
	StopObserving();
	Super::OnInstanceDestroyed(OwnerComp);
}

FString UBTDecorator_RPGBossPhase::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s\nInCombat; phase %s (%s)"), *Super::GetStaticDescription(),
		*RequiredPhase.ToString(), bExactMatch ? TEXT("exact") : TEXT("including children"));
}
