#pragma once

#include "CoreMinimal.h"
#include "RPGSecurityTypes.generated.h"

/** Server-observed classes of suspicious gameplay activity. */
UENUM(BlueprintType)
enum class ERPGSecurityViolationType : uint8
{
	MovementSpeed,
	MovementDiscontinuity,
	AbilityActivationRate,
	InvalidTargetData,
	InvalidCombatHit,
	InvalidDamage,
	RestrictedActionAttempt,
	EnforcementStateChanged,
	PlayerRemoval
};

UENUM(BlueprintType)
enum class ERPGSecurityViolationSeverity : uint8
{
	Low,
	Medium,
	High,
	Critical
};

/** Server-owned response level derived only from the authoritative risk score. */
UENUM(BlueprintType)
enum class ERPGSecurityEnforcementState : uint8
{
	Monitoring,
	Elevated,
	Restricted,
	RemovalRecommended
};

/** One server-owned security observation. This is never authored by a client. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSecurityViolation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Security")
	ERPGSecurityViolationType Type = ERPGSecurityViolationType::MovementSpeed;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Security")
	ERPGSecurityViolationSeverity Severity =
		ERPGSecurityViolationSeverity::Low;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Security")
	float Score = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Security")
	double ServerTimeSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Security")
	FString Detail;
};

/** Conservative movement tolerances layered on top of UE network movement. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGMovementSecurityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.02", Units = "s"))
	float SampleIntervalSeconds = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "1.0"))
	float DistanceToleranceMultiplier = 1.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "1.0", Units = "cm"))
	float FixedPositionTolerance = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "1.0"))
	float WalkingSpeedToleranceMultiplier = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkingSpeedTolerance = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "100.0", Units = "cm"))
	float DiscontinuityDistance = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.1", Units = "s"))
	float MaximumSampleGapSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "1"))
	int32 ConsecutiveSpeedSamplesBeforeReport = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (ClampMin = "0.0", Units = "s"))
	float SpawnGraceSeconds = 2.0f;
};

/** Server admission limits for player-authored GAS activations. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAbilitySecurityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability",
		meta = (ClampMin = "0.1", Units = "s"))
	float ActivationWindowSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability",
		meta = (ClampMin = "1"))
	int32 MaximumActivationsPerWindow = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ability",
		meta = (ClampMin = "0.0", Units = "s"))
	float MinimumSameAbilityIntervalSeconds = 0.025f;
};

/** Global hard limits applied after skill-specific validation. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCombatSecurityConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat",
		meta = (ClampMin = "1.0"))
	float MaximumDamageMagnitude = 1000000000.0f;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSecurityScoringConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring",
		meta = (ClampMin = "1.0"))
	float RiskThreshold = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring",
		meta = (ClampMin = "0.0"))
	float RiskDecayPerSecond = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Logging")
	bool bLogViolations = true;
};

/** Staged response policy. Permanent account bans are deliberately out of scope. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSecurityEnforcementConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "1.0"))
	float ElevatedRiskThreshold = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "1.0"))
	float RestrictedRiskThreshold = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "1.0"))
	float RemovalRiskThreshold = 100.0f;

	/** A lower state is restored only below Threshold * RecoveryRatio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "0.1", ClampMax = "1.0"))
	float RecoveryRatio = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled"))
	bool bBlockProtectedActionsWhenRestricted = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	float DeniedActionRiskScore = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled", ClampMin = "0.1", Units = "s"))
	float DeniedActionReportIntervalSeconds = 1.0f;

	/** Off by default. Enable only after production false-positive tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled"))
	bool bAutomaticallyRemovePlayer = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enforcement",
		meta = (EditCondition = "bEnabled && bAutomaticallyRemovePlayer",
			ClampMin = "0.1", ClampMax = "30.0", Units = "s"))
	float AutomaticRemovalDelaySeconds = 2.0f;

	bool IsValid(FString* OutReason = nullptr) const;
};

/** Runtime policy resolved from a Security Policy DataAsset or component fallback. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSecurityPolicyConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FRPGMovementSecurityConfig Movement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FRPGAbilitySecurityConfig Ability;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FRPGCombatSecurityConfig Combat;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FRPGSecurityScoringConfig Scoring;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Policy")
	FRPGSecurityEnforcementConfig Enforcement;
};

/** Optional movement budget authored on a skill definition. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillMovementSecurityProfile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (EditCondition = "bEnabled", ClampMin = "0.01", ClampMax = "10.0", Units = "s"))
	float DurationSeconds = 0.5f;

	/** Total extra distance budget for the complete movement window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0", Units = "cm"))
	float ExtraDistance = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (EditCondition = "bEnabled"))
	FName Reason = TEXT("SkillMovement");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement",
		meta = (EditCondition = "bEnabled"))
	bool bResetBaselineWhenFinished = false;
};

/** Per-skill server constraints authored beside targeting and execution data. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSkillSecurityProfile
{
	GENERATED_BODY()

	/** Client prediction may present hits, but gameplay hit queries remain authoritative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Validation")
	bool bRequireAuthorityHitQuery = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Validation",
		meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumServerHitDistance = 5000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Validation",
		meta = (ClampMin = "0.0", Units = "cm"))
	float HitLocationTolerance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Validation",
		meta = (ClampMin = "1"))
	int32 MaximumTargetsPerQuery = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Validation",
		meta = (ClampMin = "1"))
	int32 MaximumHitsPerActivation = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage",
		meta = (ClampMin = "1.0"))
	float MaximumDamagePerHit = 10000000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	FRPGSkillMovementSecurityProfile AuthorizedMovement;

	bool IsValid(FString* OutReason = nullptr) const;
};

/** Plain sample used by runtime validation and deterministic automation tests. */
struct PROJECT_RPG_API FRPGMovementSecuritySample
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	double ServerTimeSeconds = 0.0;
	float DeclaredMaximumSpeed = 0.0f;
	float AuthorizedExtraDistance = 0.0f;
	/** Root motion or an explicitly server-authorized displacement may bypass the horizontal cap. */
	bool bSkipHorizontalSpeedCheck = false;
};

struct PROJECT_RPG_API FRPGMovementSecurityResult
{
	bool bValid = true;
	bool bSampleGapTooLarge = false;
	bool bSpeedViolation = false;
	bool bDiscontinuity = false;
	float DeltaSeconds = 0.0f;
	float Distance = 0.0f;
	float HorizontalSpeed = 0.0f;
	float BaseAllowedDistance = 0.0f;
	float AllowedDistance = 0.0f;
	float AuthorizedDistanceUsed = 0.0f;
	float AllowedWalkingSpeed = 0.0f;
};

/** Deterministic result for one server-side ability admission decision. */
struct PROJECT_RPG_API FRPGAbilityActivationSecurityResult
{
	bool bValid = true;
	bool bInvalidServerTime = false;
	bool bInvalidPolicy = false;
	bool bSameAbilityIntervalViolation = false;
	bool bActivationWindowViolation = false;
	int32 RecentActivationCount = 0;
	float SecondsSinceSameAbility = TNumericLimits<float>::Max();
};

enum class ERPGCombatHitValidationFailure : uint8
{
	None,
	InvalidSpatialData,
	InvalidLimits,
	RangeExceeded,
	ImpactPointOutsideTarget
};

/** Actor-independent spatial snapshot for a server-produced combat hit. */
struct PROJECT_RPG_API FRPGCombatHitSecuritySample
{
	FVector SourceLocation = FVector::ZeroVector;
	FVector TargetBoundsOrigin = FVector::ZeroVector;
	FVector TargetBoundsExtent = FVector::ZeroVector;
	FVector ImpactPoint = FVector::ZeroVector;
	float MaximumDistance = 0.0f;
	float HitLocationTolerance = 0.0f;
};

struct PROJECT_RPG_API FRPGCombatHitSecurityResult
{
	bool bValid = true;
	ERPGCombatHitValidationFailure Failure =
		ERPGCombatHitValidationFailure::None;
	float SourceDistance = 0.0f;
	float AllowedDistance = 0.0f;
	float ImpactPointError = 0.0f;
};

/** Client-authored targeting snapshot compared with server-known state. */
struct PROJECT_RPG_API FRPGTargetDataSecuritySample
{
	FVector ServerSourceLocation = FVector::ZeroVector;
	FVector SubmittedSourceLocation = FVector::ZeroVector;
	FVector SubmittedTargetLocation = FVector::ZeroVector;
	FVector SubmittedAimDirection = FVector::ForwardVector;
	FVector ServerAimDirection = FVector::ForwardVector;
	float MaximumRange = 0.0f;
	float SourceLocationTolerance = 0.0f;
	float RangeTolerance = 0.0f;
	float AimToleranceDegrees = 0.0f;
	bool bFlattenAim = false;
	bool bValidateAim = true;
};

struct PROJECT_RPG_API FRPGTargetDataSecurityResult
{
	bool bValid = true;
	bool bInvalidSpatialData = false;
	bool bInvalidLimits = false;
	bool bSourceLocationViolation = false;
	bool bRangeViolation = false;
	bool bVerticalRangeViolation = false;
	bool bAimViolation = false;
	float SourceLocationError = 0.0f;
	float TargetDistance = 0.0f;
	float AimErrorDegrees = 0.0f;
	FVector ValidatedAimDirection = FVector::ZeroVector;
};

/** Stateless math kept separate from enforcement so tolerances are testable. */
struct PROJECT_RPG_API FRPGSecurityValidationMath
{
	static FRPGMovementSecurityResult ValidateMovement(
		const FRPGMovementSecuritySample& Previous,
		const FRPGMovementSecuritySample& Current,
		const FRPGMovementSecurityConfig& Config);

	static FRPGAbilityActivationSecurityResult ValidateAbilityActivation(
		FName AbilityKey,
		double ServerTimeSeconds,
		TConstArrayView<double> ActivationHistory,
		const TMap<FName, double>& LastActivationByAbility,
		const FRPGAbilitySecurityConfig& Config);

	static FRPGCombatHitSecurityResult ValidateCombatHit(
		const FRPGCombatHitSecuritySample& Sample);

	static bool IsDamageMagnitudeValid(
		float Damage,
		float MaximumDamageMagnitude);

	static FRPGTargetDataSecurityResult ValidateTargetData(
		const FRPGTargetDataSecuritySample& Sample);
};

/** Stateless enforcement transitions used by runtime code and automation tests. */
struct PROJECT_RPG_API FRPGSecurityEnforcementMath
{
	static ERPGSecurityEnforcementState ResolveState(
		float RiskScore,
		ERPGSecurityEnforcementState CurrentState,
		const FRPGSecurityEnforcementConfig& Config);
};
