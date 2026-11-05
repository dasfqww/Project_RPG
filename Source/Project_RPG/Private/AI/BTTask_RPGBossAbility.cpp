#include "AI/BTTask_RPGBossAbility.h"

#include "Ability/RPGBossGameplayAbility.h"
#include "AI/RPGBossDefinition.h"
#include "AI/RPGBossPatternExecutor.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UBTTask_RPGBossAbility::UBTTask_RPGBossAbility()
{
	NodeName = TEXT("Execute Boss Ability");
	bCreateNodeInstance = true;
	bNotifyTick = true;
	bNotifyTaskFinished = true;
}

EBTNodeResult::Type UBTTask_RPGBossAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Cleanup();
	bEnded = bCancelled = false;
	AAIController* Controller = OwnerComp.GetAIOwner();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !Pawn->HasAuthority() || (!bUsePhasePatterns && !AbilityClass) || !FMath::IsFinite(TimeoutSeconds) ||
		TimeoutSeconds < 0.01f || TimeoutSeconds > 600.f)
	{
		return EBTNodeResult::Failed;
	}
	const URPGBossAIComponent* Boss = Pawn->FindComponentByClass<URPGBossAIComponent>();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	// Also gate explicit-class tasks if another system starts a BT during a mechanic phase.
	// Do not block the GAS base: the encounter may still launch a mechanic directly.
	if (Phase && Phase->bSuspendCombatAI) { return EBTNodeResult::Failed; }
	ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	const int32 SelectionRevision = Boss ? Boss->GetBossState().Revision : INDEX_NONE;
	const FGameplayAbilitySpecHandle SelectedHandle = SelectAbilityHandle(Pawn);
	FGameplayAbilitySpec* Spec = ASC.IsValid() ? ASC->FindAbilitySpecFromHandle(SelectedHandle) : nullptr;
	if (!Spec || Spec->IsActive()) { Cleanup(); return EBTNodeResult::Failed; }
	Ability = Cast<URPGBossGameplayAbility>(Spec->GetPrimaryInstance());
	if (!Ability.IsValid() || Ability->GetBossExecutionSerial() == MAX_uint64)
	{
		Cleanup(); return EBTNodeResult::Failed;
	}
	Handle = Spec->Handle;
	ExecutionSerial = Ability->GetBossExecutionSerial() + 1;
	Deadline = Pawn->GetWorld()->GetTimeSeconds() + TimeoutSeconds;
	EndedDelegate = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::OnAbilityEnded);
	bOwnedExecution = true;
	// End may be synchronous. The delegate only records a result, never reenters BT execution.
	const bool bActivated = SelectedPatternId.IsValid() && Boss
		? Boss->GetPatternExecutor()->TryActivate(SelectedPatternId, SelectionRevision)
		: ASC->TryActivateAbility(Handle, false);
	if (!bActivated || !Ability.IsValid() || (!bEnded && !Ability->IsBossExecutionValid()))
	{
		Cleanup(); return EBTNodeResult::Failed;
	}
	if (bEnded)
	{
		const EBTNodeResult::Type Result = bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
		Cleanup();
		return Result;
	}
	return EBTNodeResult::InProgress;
}

FGameplayAbilitySpecHandle UBTTask_RPGBossAbility::SelectAbilityHandle(APawn* Pawn)
{
	SelectedPatternId = {};
	if (!ASC.IsValid()) { return FGameplayAbilitySpecHandle(); }
	if (!bUsePhasePatterns)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
		return Spec ? Spec->Handle : FGameplayAbilitySpecHandle();
	}
	const URPGBossAIComponent* Boss = Pawn->FindComponentByClass<URPGBossAIComponent>();
	const FRPGBossPhaseDefinition* Phase = Boss ? Boss->GetActivePhaseDefinition() : nullptr;
	if (!Phase || !ASC->AbilityActorInfo.IsValid()) { return FGameplayAbilitySpecHandle(); }
	if (!Phase->Patterns.IsEmpty()) return Boss->GetPatternExecutor()->Select(SelectedPatternId);
	const FRPGBossState SelectionState = Boss->GetBossState();
	const bool bRandomize = Phase->bRandomizePatterns;
	FGameplayAbilitySpecHandle SelectedHandle;
	int32 EligibleCount = 0;
	for (const TSubclassOf<URPGBossGameplayAbility>& Pattern : Phase->AbilityPatterns)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(Pattern);
		UGameplayAbility* Candidate = Spec ? Spec->GetPrimaryInstance() : nullptr;
		if (!Candidate || Spec->IsActive()) { continue; }
		const FGameplayAbilitySpecHandle CandidateHandle = Spec->Handle;
		const bool bCanActivate = Candidate->CanActivateAbility(CandidateHandle, ASC->AbilityActorInfo.Get());
		// CanActivate may run authored callbacks. Do not select an old phase after
		// a reset, phase transition or another state change during that callback.
		if (!ASC.IsValid() || !ASC->AbilityActorInfo.IsValid() || !IsValid(Boss) ||
			Boss->GetBossState().Revision != SelectionState.Revision || Boss->GetActivePhaseDefinition() != Phase)
			return FGameplayAbilitySpecHandle();
		if (bCanActivate)
		{
			if (!bRandomize) { SelectedHandle = CandidateHandle; break; }
			// Reservoir sampling: each eligible pattern has equal probability,
			// with no shared node state or intermediate ability grants/activations.
			++EligibleCount;
			if (FMath::RandHelper(EligibleCount) == 0) { SelectedHandle = CandidateHandle; }
		}
	}
#if !UE_BUILD_SHIPPING
	if (SelectedHandle.IsValid() && FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2EAttackPresentation")))
	{
		const FGameplayAbilitySpec* Selected = ASC->FindAbilitySpecFromHandle(SelectedHandle);
		if (Selected && Selected->Ability)
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_PHASE_PATTERN_SELECTED Phase=%d Ability=%s ServerOnly=1"),
				SelectionState.PhaseIndex, *Selected->Ability->GetClass()->GetName());
	}
#endif
	return SelectedHandle;
}

void UBTTask_RPGBossAbility::OnAbilityEnded(const FAbilityEndedData& Data)
{
	if (bOwnedExecution && !bEnded && Data.AbilitySpecHandle == Handle && Data.AbilityThatEnded == Ability.Get() &&
		Ability.IsValid() && Ability->GetBossExecutionSerial() == ExecutionSerial)
	{
		bEnded = true;
		bCancelled = Data.bWasCancelled;
	}
}

void UBTTask_RPGBossAbility::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	if (bEnded)
	{
		const EBTNodeResult::Type Result = bCancelled ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
		Cleanup();
		FinishLatentTask(OwnerComp, Result);
		return;
	}
	if (!ASC.IsValid() || !Ability.IsValid() || !Ability->IsActive() ||
		Ability->GetBossExecutionSerial() != ExecutionSerial || !Ability->IsBossExecutionValid() ||
		OwnerComp.GetWorld()->GetTimeSeconds() >= Deadline)
	{
		Cleanup();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

void UBTTask_RPGBossAbility::Cleanup()
{
	UAbilitySystemComponent* PreviousASC = ASC.Get();
	URPGBossGameplayAbility* PreviousAbility = Ability.Get();
	const FGameplayAbilitySpecHandle PreviousHandle = Handle;
	const bool bCancel = bOwnedExecution && !bEnded && PreviousAbility && PreviousAbility->IsActive() &&
		PreviousAbility->GetBossExecutionSerial() == ExecutionSerial;
	if (PreviousASC) { PreviousASC->OnAbilityEnded.Remove(EndedDelegate); }
	EndedDelegate.Reset();
	bOwnedExecution = false;
	ASC.Reset();
	Ability.Reset();
	Handle = FGameplayAbilitySpecHandle();
	SelectedPatternId = {};
	if (bCancel && PreviousASC && PreviousASC->IsOwnerActorAuthoritative())
	{
		PreviousASC->CancelAbilityHandle(PreviousHandle);
	}
}

EBTNodeResult::Type UBTTask_RPGBossAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Cleanup();
	return EBTNodeResult::Aborted;
}

void UBTTask_RPGBossAbility::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult)
{
	Cleanup();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

void UBTTask_RPGBossAbility::OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp)
{
	Cleanup();
	Super::OnInstanceDestroyed(OwnerComp);
}
