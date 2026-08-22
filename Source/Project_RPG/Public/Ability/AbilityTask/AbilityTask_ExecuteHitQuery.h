#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "AbilityTask_ExecuteHitQuery.generated.h"

/** Controls which network instance performs a one-shot query. */
UENUM(BlueprintType)
enum class ERPGHitQueryNetPolicy : uint8
{
	/** Authoritative result used for damage and Gameplay Effects. */
	AuthorityOnly,

	/** Client-side prediction or presentation only. */
	LocallyControlledOnly,

	/** Runs independently on both authority and locally controlled instances. */
	AuthorityAndLocallyControlled
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FRPGHitQueryTaskDelegate,
	const FGameplayAbilityTargetDataHandle&, TargetData,
	const TArray<FRPGHitQueryResult>&, QueryResults);

/**
 * One-shot GAS bridge for the shared hit-query subsystem.
 *
 * The task returns standard TargetData and still does not apply effects. Use
 * AuthorityOnly for gameplay-affecting queries; the other policies are for
 * predicted presentation and local feedback.
 */
UCLASS()
class PROJECT_RPG_API UAbilityTask_ExecuteHitQuery : public UAbilityTask
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityTasks",
		meta = (DisplayName = "Execute Hit Query",
			HidePin = "OwningAbility",
			DefaultToSelf = "OwningAbility",
			BlueprintInternalUseOnly = "true"))
	static UAbilityTask_ExecuteHitQuery* ExecuteHitQuery(
		UGameplayAbility* OwningAbility,
		const FRPGHitQueryContext& QueryContext,
		ERPGHitQueryNetPolicy NetPolicy = ERPGHitQueryNetPolicy::AuthorityOnly);

	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FRPGHitQueryTaskDelegate OnTargetsFound;

	UPROPERTY(BlueprintAssignable)
	FRPGHitQueryTaskDelegate OnNoTargets;

private:
	UPROPERTY()
	FRPGHitQueryContext CachedQueryContext;

	ERPGHitQueryNetPolicy CachedNetPolicy = ERPGHitQueryNetPolicy::AuthorityOnly;
};
