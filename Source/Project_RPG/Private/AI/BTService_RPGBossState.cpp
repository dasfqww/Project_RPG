#include "AI/BTService_RPGBossState.h"

#include "AI/RPGBossAIComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"

UBTService_RPGBossState::UBTService_RPGBossState()
{
	NodeName = TEXT("RPG Boss State (Server)");
	Interval = 0.1f;
	RandomDeviation = 0.0f;
	bCallTickOnSearchStart = true;
	INIT_SERVICE_NODE_NOTIFY_FLAGS();
	InCombatKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, InCombatKey));
	PhaseIndexKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, PhaseIndexKey));
	EnragedKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(ThisClass, EnragedKey));
	InCombatKey.SelectedKeyName = TEXT("BossInCombat");
	PhaseIndexKey.SelectedKeyName = TEXT("BossPhaseIndex");
	EnragedKey.SelectedKeyName = TEXT("BossEnraged");
}

void UBTService_RPGBossState::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);
	if (UBlackboardData* Blackboard = GetBlackboardAsset())
	{
		InCombatKey.ResolveSelectedKey(*Blackboard);
		PhaseIndexKey.ResolveSelectedKey(*Blackboard);
		EnragedKey.ResolveSelectedKey(*Blackboard);
	}
}

void UBTService_RPGBossState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
	const AAIController* Controller = OwnerComp.GetAIOwner();
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Controller || !Controller->HasAuthority() || !Blackboard)
	{
		return;
	}
	const APawn* Pawn = Controller->GetPawn();
	const URPGBossAIComponent* Boss = Pawn ? Pawn->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	const FRPGBossState Snapshot = Boss ? Boss->GetBossState() : FRPGBossState();
	Blackboard->PauseObserverNotifications();
	if (InCombatKey.SelectedKeyType == UBlackboardKeyType_Bool::StaticClass())
	{
		Blackboard->SetValueAsBool(InCombatKey.SelectedKeyName, Snapshot.EncounterState == ERPGBossEncounterState::InCombat);
	}
	if (PhaseIndexKey.SelectedKeyType == UBlackboardKeyType_Int::StaticClass())
	{
		Blackboard->SetValueAsInt(PhaseIndexKey.SelectedKeyName, Snapshot.PhaseIndex);
	}
	if (EnragedKey.SelectedKeyType == UBlackboardKeyType_Bool::StaticClass())
	{
		Blackboard->SetValueAsBool(EnragedKey.SelectedKeyName, Snapshot.bEnraged);
	}
	Blackboard->ResumeObserverNotifications(true);
}
