#include "Ability/AbilityTask/AbilityTask_ExecuteHitQuery.h"

#include "Combat/HitQuery/RPGHitQuerySubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_ExecuteHitQuery)

UAbilityTask_ExecuteHitQuery* UAbilityTask_ExecuteHitQuery::ExecuteHitQuery(
	UGameplayAbility* OwningAbility,
	const FRPGHitQueryContext& QueryContext,
	const ERPGHitQueryNetPolicy NetPolicy)
{
	UAbilityTask_ExecuteHitQuery* Task =
		NewAbilityTask<UAbilityTask_ExecuteHitQuery>(OwningAbility);
	Task->CachedQueryContext = QueryContext;
	Task->CachedNetPolicy = NetPolicy;
	return Task;
}

void UAbilityTask_ExecuteHitQuery::Activate()
{
	Super::Activate();

	const FGameplayAbilityActorInfo* ActorInfo =
		Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const bool bIsAuthority = AvatarActor && AvatarActor->HasAuthority();
	const bool bIsLocallyControlled = ActorInfo && ActorInfo->IsLocallyControlled();

	const bool bShouldExecute =
		CachedNetPolicy == ERPGHitQueryNetPolicy::AuthorityOnly
			? bIsAuthority
			: CachedNetPolicy == ERPGHitQueryNetPolicy::LocallyControlledOnly
				? bIsLocallyControlled
				: bIsAuthority || bIsLocallyControlled;

	if (!bShouldExecute || !ShouldBroadcastAbilityTaskDelegates())
	{
		EndTask();
		return;
	}

	if (!CachedQueryContext.SourceActor)
	{
		CachedQueryContext.SourceActor = AvatarActor;
	}

	UWorld* World = GetWorld();
	URPGHitQuerySubsystem* HitQuerySubsystem =
		World ? World->GetSubsystem<URPGHitQuerySubsystem>() : nullptr;

	TArray<FRPGHitQueryResult> QueryResults;
	const bool bFoundTargets =
		HitQuerySubsystem &&
		HitQuerySubsystem->ExecuteHitQuery(CachedQueryContext, QueryResults);

	FGameplayAbilityTargetDataHandle TargetData;
	for (const FRPGHitQueryResult& Result : QueryResults)
	{
		FGameplayAbilityTargetData_SingleTargetHit* SingleTargetData =
			new FGameplayAbilityTargetData_SingleTargetHit();
		SingleTargetData->HitResult = Result.HitResult;
		TargetData.Add(SingleTargetData);
	}

	if (ShouldBroadcastAbilityTaskDelegates())
	{
		if (bFoundTargets)
		{
			OnTargetsFound.Broadcast(TargetData, QueryResults);
		}
		else
		{
			OnNoTargets.Broadcast(TargetData, QueryResults);
		}
	}

	EndTask();
}
