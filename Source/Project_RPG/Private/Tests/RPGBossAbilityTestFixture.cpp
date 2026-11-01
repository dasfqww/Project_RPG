#include "Tests/RPGBossAbilityTestFixture.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossPatternCooldown, "Tests.RaidBoss.Pattern.Cooldown");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTimedPatternCooldown, "Tests.RaidBoss.Pattern.TimedCooldown");

URPGBossTimedCooldownPatternTestAbility::URPGBossTimedCooldownPatternTestAbility()
{
	PatternCooldownSeconds = .8f;
	PatternCooldownTag = TAG_BossTimedPatternCooldown;
}

URPGBossCooldownPatternTestAbility::URPGBossCooldownPatternTestAbility()
{
	PatternCooldownTags.AddTag(TAG_BossPatternCooldown);
}

bool URPGBossTestAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const URPGBossAbilityTestProbe* Probe = Cast<URPGBossAbilityTestProbe>(GetSourceObject(Handle, ActorInfo));
	if (Probe && Probe->bResetOnCanActivate)
	{
		AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
		if (URPGBossAIComponent* EncounterState = Avatar ? Avatar->FindComponentByClass<URPGBossAIComponent>() : nullptr)
			EncounterState->BeginReset();
	}
	return Probe && !Probe->bBlockActivation && Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGBossTestAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	if (IsActive() && IsBossExecutionValid())
	{
		URPGBossAbilityTestProbe* Probe = CastChecked<URPGBossAbilityTestProbe>(GetSourceObject(Handle, ActorInfo));
		const uint64 StartedSerial = GetBossExecutionSerial();
		if (Probe->bCommitOnActivate)
		{
			const bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo);
			if (!IsActive() || GetBossExecutionSerial() != StartedSerial) { return; }
			if (!bCommitted || !IsBossExecutionValid()) { FinishTest(true); return; }
		}
		++Probe->Activated;
		Probe->SeenTarget = GetBossExecutionTarget();
		if (Probe->bEndImmediately || Probe->bCancelImmediately) { FinishTest(Probe->bCancelImmediately); }
	}
}

void URPGBossTestAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (IsActive())
	{
		if (URPGBossAbilityTestProbe* Probe = Cast<URPGBossAbilityTestProbe>(GetSourceObject(Handle, ActorInfo)))
		{
			++Probe->Ended;
			Probe->Cancelled += bWasCancelled ? 1 : 0;
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGBossTestAbility::FinishTest(bool bCancel)
{
	if (IsActive()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, bCancel); }
}

void UBTTask_RPGBossTestAbility::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
	if (Probe)
	{
		++Probe->FinishedTasks;
		Probe->LastResult = TaskResult;
	}
}

UBehaviorTreeComponent* StartBossAbilityTestTree(AAIController* Controller, URPGBossAbilityTestProbe* Probe,
	float Timeout, TSubclassOf<URPGBossGameplayAbility> AbilityClass, bool bUsePhasePatterns)
{
	UBehaviorTree* Tree = NewObject<UBehaviorTree>(Controller);
	Tree->BlackboardAsset = NewObject<UBlackboardData>(Tree);
	Tree->RootNode = NewObject<UBTComposite_Sequence>(Tree);
	UBTTask_RPGBossTestAbility* Task = NewObject<UBTTask_RPGBossTestAbility>(Tree);
	Task->AbilityClass = AbilityClass;
	if (!Task->AbilityClass) { Task->AbilityClass = URPGBossTestAbility::StaticClass(); }
	Task->TimeoutSeconds = Timeout;
	Task->bUsePhasePatterns = bUsePhasePatterns;
	Task->Probe = Probe;
	FBTCompositeChild& Child = Tree->RootNode->Children.AddDefaulted_GetRef();
	Child.ChildTask = Task;
	UBlackboardComponent* Blackboard = nullptr;
	if (!Controller->UseBlackboard(Tree->BlackboardAsset, Blackboard)) { return nullptr; }
	UBehaviorTreeComponent* BT = Cast<UBehaviorTreeComponent>(Controller->BrainComponent);
	if (!BT)
	{
		BT = NewObject<UBehaviorTreeComponent>(Controller);
		BT->RegisterComponent();
		Controller->BrainComponent = BT;
	}
	BT->StartTree(*Tree, EBTExecutionMode::SingleRun);
	return BT;
}
