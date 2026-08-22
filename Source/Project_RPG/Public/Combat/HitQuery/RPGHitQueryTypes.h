#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "RPGHitQueryTypes.generated.h"

class AActor;
class UPrimitiveComponent;

/** Shapes supported by the shared player-skill, AI, and boss-pattern query layer. */
UENUM(BlueprintType)
enum class ERPGHitQueryShape : uint8
{
	Sphere,
	Capsule,
	Box,
	Sector,
	RingSector,
	LineSweep
};

/** Team relationship required between the query source and a candidate. */
UENUM(BlueprintType)
enum class ERPGHitQueryTeamRule : uint8
{
	Any,
	HostileOnly,
	FriendlyOnly
};

/** Point on a candidate used by the narrow-phase geometry test. */
UENUM(BlueprintType)
enum class ERPGHitQueryTargetPointMode : uint8
{
	ActorOrigin,
	ClosestCollisionPoint
};

/** Optional deterministic ordering before MaxResults is applied. */
UENUM(BlueprintType)
enum class ERPGHitQuerySortMode : uint8
{
	None,
	Nearest,
	Farthest
};

/**
 * Data-only shape description.
 *
 * Sector shapes are evaluated on local XY and constrained by HalfHeight on
 * local Z. BoxHalfExtent is used only by Box. Length is used by LineSweep.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitQueryShape
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	ERPGHitQueryShape Type = ERPGHitQueryShape::Sector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector LocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FRotator LocalRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", Units = "cm"))
	float Radius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", Units = "cm"))
	float InnerRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", Units = "cm"))
	float Length = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", Units = "cm"))
	float HalfHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", Units = "cm"))
	FVector BoxHalfExtent = FVector(100.0f);

	/** Full sector angle. A value of 360 produces a complete disc or ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", ClampMax = "360.0", Units = "deg"))
	float AngleDegrees = 90.0f;
};

/** Candidate eligibility and result selection rules, independent of geometry. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitQueryFilter
{
	GENERATED_BODY()

	FRPGHitQueryFilter();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	ERPGHitQueryTeamRule TeamRule = ERPGHitQueryTeamRule::HostileOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bIncludeSourceActor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	ERPGHitQueryTargetPointMode TargetPointMode =
		ERPGHitQueryTargetPointMode::ClosestCollisionPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	bool bRequireLineOfSight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter",
		meta = (EditCondition = "bRequireLineOfSight"))
	TEnumAsByte<ECollisionChannel> LineOfSightTraceChannel = ECC_Visibility;

	/** Candidates without an Ability System Component fail non-empty requirements. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Gameplay Tags")
	FGameplayTagContainer RequiredTargetTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter|Gameplay Tags")
	FGameplayTagContainer BlockedTargetTags;

	/** Zero means unlimited. Ordering is applied before this limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter",
		meta = (ClampMin = "0"))
	int32 MaxResults = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Filter")
	ERPGHitQuerySortMode SortMode = ERPGHitQuerySortMode::Nearest;
};

/** Reusable authored data consumed by skills, AI actions, and boss patterns. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitQueryProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	FRPGHitQueryShape Shape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	FRPGHitQueryFilter Filter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query|Debug")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query|Debug",
		meta = (EditCondition = "bDrawDebug"))
	FColor DebugColor = FColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query|Debug",
		meta = (EditCondition = "bDrawDebug", ClampMin = "0.0", Units = "s"))
	float DebugDuration = 1.0f;
};

/** Activation-local input. QueryTransform is commonly an actor or socket transform. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitQueryContext
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	FTransform QueryTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	FRPGHitQueryProfile Profile;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	TArray<TObjectPtr<AActor>> IgnoredActors;

	/** Maintained by an activation or persistent area to prevent repeat hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Query")
	TArray<TObjectPtr<AActor>> AlreadyHitActors;
};

/** Geometry result only. Damage and gameplay effects are intentionally separate. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitQueryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Hit Query")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Hit Query")
	TObjectPtr<UPrimitiveComponent> TargetComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Hit Query")
	FVector QueryPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Hit Query", meta = (Units = "cm"))
	float Distance = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Hit Query")
	FHitResult HitResult;
};

/** Stateless narrow-phase math, kept independent so it can be automation tested. */
struct PROJECT_RPG_API FRPGHitQueryMath
{
	static FTransform ResolveShapeTransform(
		const FTransform& QueryTransform,
		const FRPGHitQueryShape& Shape);

	static bool IsShapeValid(const FRPGHitQueryShape& Shape);

	static bool IsPointInsideShape(
		const FTransform& ShapeTransform,
		const FRPGHitQueryShape& Shape,
		const FVector& WorldPoint);
};
