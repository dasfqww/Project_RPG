#pragma once

#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "RPGSkillTargetingTypes.generated.h"

class AActor;

/** Settings shared by all skill targeting policies. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillTargetingConfig
{
	GENERATED_BODY()

	/**
	 * Rotate only the source actor's yaw to the resolved aim direction.
	 * The control rotation is deliberately left untouched so quick-cast skills
	 * do not pull the third-person camera.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bOrientSourceToAim = true;

	/** Allowed client/server avatar position drift when validating predicted aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting|Network Validation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ServerSourceLocationTolerance = 300.0f;

	/** Grace added to authored range and collision-point checks for latency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting|Network Validation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ServerRangeTolerance = 150.0f;

	/** Maximum divergence from the server-known control aim before rejection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting|Network Validation",
		meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float ServerAimToleranceDegrees = 60.0f;
};

/** Camera-centre quick-cast targeting with optional lock-on priority. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillCameraDirectionTargetingConfig
	: public FRPGSkillTargetingConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRange = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bUseLockedTargetFirst = true;

	/** Useful for melee and movement skills that must remain on the ground plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bFlattenAimDirection = false;

	/** Directional skills normally remain valid even when the camera ray hits no surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bRequireBlockingHit = false;
};

/** Camera-cone aim assist for close-range third-person skills. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillSoftTargetingConfig
	: public FRPGSkillTargetingConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRange = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "0.1", ClampMax = "180.0", Units = "deg"))
	float AssistAngleDegrees = 45.0f;

	/** Small grace applied to the authored assist cone during server validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "Targeting|Network Validation",
		meta = (ClampMin = "0.0", ClampMax = "45.0", Units = "deg"))
	float ServerAssistConeToleranceDegrees = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "0.0", Units = "cm"))
	float VerticalHalfHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	FRPGHitQueryFilter CandidateFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "0.0"))
	float AngleScoreWeight = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "0.0"))
	float DistanceScoreWeight = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bPreferLockedTarget = true;

	/** When no candidate exists, cast toward the crosshair instead of rejecting the skill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bFallbackToCameraDirection = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	TEnumAsByte<ECollisionChannel> FallbackTraceChannel = ECC_Visibility;
};

/** Crosshair-to-world projection for quick-cast ground skills. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillGroundPointTargetingConfig
	: public FRPGSkillTargetingConfig
{
	GENERATED_BODY()

	FRPGSkillGroundPointTargetingConfig();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRange = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	TEnumAsByte<ECollisionChannel> CameraTraceChannel = ECC_Visibility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	TArray<TEnumAsByte<EObjectTypeQuery>> GroundObjectTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "0.0", Units = "cm"))
	float GroundTraceHeight = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting",
		meta = (ClampMin = "1.0", Units = "cm"))
	float GroundTraceDepth = 3000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	bool bRequireGroundHit = true;
};

/** Activation-local aim result consumed by execution, hit queries, and presentation. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillTargetResult
{
	GENERATED_BODY()

	void Reset();

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	bool bIsValid = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	FVector SourceLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	FVector TargetLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	FVector AimDirection = FVector::ForwardVector;

	/** Final origin/orientation passed to the authored Hit Query profile. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	FTransform HitQueryTransform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	FHitResult AimHit;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Targeting")
	bool bOrientSourceToAim = false;
};

/**
 * Converts the project aim snapshot to standard GAS TargetData and back.
 *
 * Only player-authored spatial input is transported. Orientation flags,
 * hit-query transforms, and hit results are rebuilt from authoritative policy
 * data on the server instead of being trusted from the client.
 */
struct PROJECT_RPG_API FRPGSkillTargetDataCodec
{
	static FGameplayAbilityTargetDataHandle Encode(
		const FRPGSkillTargetResult& TargetResult);

	static bool Decode(
		const FGameplayAbilityTargetDataHandle& TargetData,
		FRPGSkillTargetResult& OutTargetResult);
};

/** Stateless scoring helpers kept independent for deterministic tests. */
struct PROJECT_RPG_API FRPGSkillTargetingMath
{
	static float CalculateSoftTargetScore(
		float AngleDegrees,
		float Distance,
		float MaxAngleDegrees,
		float MaxRange,
		float AngleWeight,
		float DistanceWeight);
};
