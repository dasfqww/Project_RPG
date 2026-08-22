#pragma once

#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "RPGHitQuerySubsystem.generated.h"

/**
 * Shared spatial query service for player skills, PvE attacks, and boss patterns.
 *
 * This subsystem discovers and filters targets. It never applies damage or
 * Gameplay Effects.
 */
UCLASS()
class PROJECT_RPG_API URPGHitQuerySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "RPG|Combat|Hit Query")
	bool ExecuteHitQuery(
		const FRPGHitQueryContext& Context,
		TArray<FRPGHitQueryResult>& OutResults) const;

	/** Applies the shared source/team/tag/line-of-sight rules to one known actor. */
	bool IsTargetEligible(
		AActor* SourceActor,
		AActor* TargetActor,
		const FVector& QueryOrigin,
		const FRPGHitQueryFilter& Filter,
		const TArray<TObjectPtr<AActor>>& IgnoredActors = {}) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Combat|Hit Query")
	static bool IsPointInsideHitShape(
		const FTransform& QueryTransform,
		const FRPGHitQueryShape& Shape,
		const FVector& WorldPoint);

	UFUNCTION(BlueprintCallable, Category = "RPG|Combat|Hit Query")
	void DrawDebugHitQuery(
		const FTransform& QueryTransform,
		const FRPGHitQueryProfile& Profile) const;
};
