#include "Security/RPGSecurityTypes.h"

namespace RPGSecurityValidation
{
	bool IsFiniteVector(const FVector& Value)
	{
		return !Value.ContainsNaN() &&
			FMath::IsFinite(Value.X) &&
			FMath::IsFinite(Value.Y) &&
			FMath::IsFinite(Value.Z);
	}
}

bool FRPGSecurityEnforcementConfig::IsValid(FString* OutReason) const
{
	auto Fail = [OutReason](const TCHAR* Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};

	if (!bEnabled)
	{
		if (OutReason)
		{
			OutReason->Reset();
		}
		return true;
	}
	if (!FMath::IsFinite(ElevatedRiskThreshold)
		|| !FMath::IsFinite(RestrictedRiskThreshold)
		|| !FMath::IsFinite(RemovalRiskThreshold)
		|| ElevatedRiskThreshold <= 0.0f
		|| RestrictedRiskThreshold <= ElevatedRiskThreshold
		|| RemovalRiskThreshold <= RestrictedRiskThreshold)
	{
		return Fail(TEXT(
			"Enforcement thresholds must be finite, positive, and strictly increasing."));
	}
	if (!FMath::IsFinite(RecoveryRatio)
		|| RecoveryRatio < 0.1f
		|| RecoveryRatio > 1.0f)
	{
		return Fail(TEXT("Enforcement RecoveryRatio must be between 0.1 and 1.0."));
	}
	if (!FMath::IsFinite(DeniedActionRiskScore)
		|| DeniedActionRiskScore < 0.0f
		|| !FMath::IsFinite(DeniedActionReportIntervalSeconds)
		|| DeniedActionReportIntervalSeconds <= 0.0f)
	{
		return Fail(TEXT("Denied-action scoring values are invalid."));
	}
	if (!FMath::IsFinite(AutomaticRemovalDelaySeconds)
		|| AutomaticRemovalDelaySeconds < 0.1f
		|| AutomaticRemovalDelaySeconds > 30.0f)
	{
		return Fail(TEXT("Automatic removal delay must be between 0.1 and 30 seconds."));
	}

	if (OutReason)
	{
		OutReason->Reset();
	}
	return true;
}

ERPGSecurityEnforcementState FRPGSecurityEnforcementMath::ResolveState(
	const float RiskScore,
	const ERPGSecurityEnforcementState CurrentState,
	const FRPGSecurityEnforcementConfig& Config)
{
	if (!Config.bEnabled)
	{
		return ERPGSecurityEnforcementState::Monitoring;
	}
	if (!FMath::IsFinite(RiskScore) || RiskScore < 0.0f)
	{
		return ERPGSecurityEnforcementState::RemovalRecommended;
	}

	const float ElevatedThreshold = FMath::Max(
		1.0f,
		FMath::IsFinite(Config.ElevatedRiskThreshold)
			? Config.ElevatedRiskThreshold
			: 25.0f);
	const float RestrictedThreshold = FMath::Max(
		ElevatedThreshold + 1.0f,
		FMath::IsFinite(Config.RestrictedRiskThreshold)
			? Config.RestrictedRiskThreshold
			: 50.0f);
	const float RemovalThreshold = FMath::Max(
		RestrictedThreshold + 1.0f,
		FMath::IsFinite(Config.RemovalRiskThreshold)
			? Config.RemovalRiskThreshold
			: 100.0f);
	const float RecoveryRatio = FMath::Clamp(
		FMath::IsFinite(Config.RecoveryRatio)
			? Config.RecoveryRatio
			: 0.75f,
		0.1f,
		1.0f);

	ERPGSecurityEnforcementState DesiredState =
		ERPGSecurityEnforcementState::Monitoring;
	if (RiskScore >= RemovalThreshold)
	{
		DesiredState = ERPGSecurityEnforcementState::RemovalRecommended;
	}
	else if (RiskScore >= RestrictedThreshold)
	{
		DesiredState = ERPGSecurityEnforcementState::Restricted;
	}
	else if (RiskScore >= ElevatedThreshold)
	{
		DesiredState = ERPGSecurityEnforcementState::Elevated;
	}

	if (static_cast<uint8>(DesiredState) >= static_cast<uint8>(CurrentState))
	{
		return DesiredState;
	}

	float CurrentRecoveryThreshold = 0.0f;
	switch (CurrentState)
	{
	case ERPGSecurityEnforcementState::RemovalRecommended:
		CurrentRecoveryThreshold = RemovalThreshold * RecoveryRatio;
		break;
	case ERPGSecurityEnforcementState::Restricted:
		CurrentRecoveryThreshold = RestrictedThreshold * RecoveryRatio;
		break;
	case ERPGSecurityEnforcementState::Elevated:
		CurrentRecoveryThreshold = ElevatedThreshold * RecoveryRatio;
		break;
	case ERPGSecurityEnforcementState::Monitoring:
	default:
		break;
	}
	return RiskScore >= CurrentRecoveryThreshold
		? CurrentState
		: DesiredState;
}

bool FRPGSkillSecurityProfile::IsValid(FString* OutReason) const
{
	auto Fail = [OutReason](const TCHAR* Reason)
	{
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};

	if (!FMath::IsFinite(MaximumServerHitDistance) ||
		MaximumServerHitDistance <= 0.0f)
	{
		return Fail(TEXT("MaximumServerHitDistance must be finite and positive."));
	}
	if (!FMath::IsFinite(HitLocationTolerance) || HitLocationTolerance < 0.0f)
	{
		return Fail(TEXT("HitLocationTolerance must be finite and non-negative."));
	}
	if (MaximumTargetsPerQuery <= 0 || MaximumHitsPerActivation <= 0)
	{
		return Fail(TEXT("Skill hit count limits must be positive."));
	}
	if (!FMath::IsFinite(MaximumDamagePerHit) || MaximumDamagePerHit <= 0.0f)
	{
		return Fail(TEXT("MaximumDamagePerHit must be finite and positive."));
	}
	if (AuthorizedMovement.bEnabled &&
		(!FMath::IsFinite(AuthorizedMovement.DurationSeconds) ||
		 AuthorizedMovement.DurationSeconds <= 0.0f ||
		 !FMath::IsFinite(AuthorizedMovement.ExtraDistance) ||
		 AuthorizedMovement.ExtraDistance < 0.0f))
	{
		return Fail(TEXT("Authorized movement duration and distance are invalid."));
	}

	if (OutReason)
	{
		OutReason->Reset();
	}
	return true;
}

FRPGMovementSecurityResult FRPGSecurityValidationMath::ValidateMovement(
	const FRPGMovementSecuritySample& Previous,
	const FRPGMovementSecuritySample& Current,
	const FRPGMovementSecurityConfig& Config)
{
	FRPGMovementSecurityResult Result;
	const double DeltaSeconds =
		Current.ServerTimeSeconds - Previous.ServerTimeSeconds;
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= UE_DOUBLE_SMALL_NUMBER)
	{
		return Result;
	}

	Result.DeltaSeconds = static_cast<float>(DeltaSeconds);
	if (DeltaSeconds > FMath::Max(
		static_cast<double>(Config.MaximumSampleGapSeconds),
		UE_DOUBLE_SMALL_NUMBER))
	{
		Result.bSampleGapTooLarge = true;
		return Result;
	}

	if (Previous.Location.ContainsNaN() || Current.Location.ContainsNaN() ||
		Previous.Velocity.ContainsNaN() || Current.Velocity.ContainsNaN())
	{
		Result.bValid = false;
		Result.bDiscontinuity = true;
		return Result;
	}

	const FVector Delta = Current.Location - Previous.Location;
	Result.Distance = Delta.Size();
	Result.HorizontalSpeed = Delta.Size2D() / Result.DeltaSeconds;

	const float KinematicSpeed = FMath::Max3(
		FMath::Max(0.0f, Current.DeclaredMaximumSpeed),
		static_cast<float>(Previous.Velocity.Size()),
		static_cast<float>(Current.Velocity.Size()));
	Result.BaseAllowedDistance =
		KinematicSpeed * Result.DeltaSeconds *
			FMath::Max(1.0f, Config.DistanceToleranceMultiplier) +
		FMath::Max(0.0f, Config.FixedPositionTolerance);
	const float AuthorizedDistance =
		FMath::Max(0.0f, Current.AuthorizedExtraDistance);
	Result.AllowedDistance = Result.BaseAllowedDistance + AuthorizedDistance;
	Result.AuthorizedDistanceUsed = FMath::Clamp(
		Result.Distance - Result.BaseAllowedDistance,
		0.0f,
		AuthorizedDistance);

	Result.AllowedWalkingSpeed =
		FMath::Max(0.0f, Current.DeclaredMaximumSpeed) *
			FMath::Max(1.0f, Config.WalkingSpeedToleranceMultiplier) +
		FMath::Max(0.0f, Config.WalkingSpeedTolerance);

	const bool bDistanceViolation =
		Result.Distance > Result.AllowedDistance;
	Result.bSpeedViolation =
		!Current.bSkipHorizontalSpeedCheck &&
		Result.HorizontalSpeed > Result.AllowedWalkingSpeed;
	Result.bDiscontinuity =
		Result.Distance > FMath::Max(
			FMath::Max(0.0f, Config.DiscontinuityDistance),
			Result.AllowedDistance * 2.0f);
	Result.bValid = !bDistanceViolation && !Result.bSpeedViolation;
	return Result;
}

FRPGAbilityActivationSecurityResult
FRPGSecurityValidationMath::ValidateAbilityActivation(
	const FName AbilityKey,
	const double ServerTimeSeconds,
	const TConstArrayView<double> ActivationHistory,
	const TMap<FName, double>& LastActivationByAbility,
	const FRPGAbilitySecurityConfig& Config)
{
	FRPGAbilityActivationSecurityResult Result;
	if (!FMath::IsFinite(ServerTimeSeconds))
	{
		Result.bValid = false;
		Result.bInvalidServerTime = true;
		return Result;
	}
	if (!FMath::IsFinite(Config.ActivationWindowSeconds) ||
		Config.ActivationWindowSeconds <= 0.0f ||
		Config.MaximumActivationsPerWindow <= 0 ||
		!FMath::IsFinite(Config.MinimumSameAbilityIntervalSeconds) ||
		Config.MinimumSameAbilityIntervalSeconds < 0.0f)
	{
		Result.bValid = false;
		Result.bInvalidPolicy = true;
		return Result;
	}

	if (!AbilityKey.IsNone())
	{
		if (const double* LastActivation =
			LastActivationByAbility.Find(AbilityKey))
		{
			const double ElapsedSeconds =
				ServerTimeSeconds - *LastActivation;
			Result.SecondsSinceSameAbility =
				FMath::IsFinite(ElapsedSeconds)
					? static_cast<float>(ElapsedSeconds)
					: -1.0f;
			Result.bSameAbilityIntervalViolation =
				!FMath::IsFinite(ElapsedSeconds) ||
				ElapsedSeconds < static_cast<double>(
					Config.MinimumSameAbilityIntervalSeconds);
		}
	}

	const double WindowStart = ServerTimeSeconds -
		static_cast<double>(Config.ActivationWindowSeconds);
	for (const double Timestamp : ActivationHistory)
	{
		if (FMath::IsFinite(Timestamp) &&
			Timestamp >= WindowStart && Timestamp <= ServerTimeSeconds)
		{
			++Result.RecentActivationCount;
		}
	}
	Result.bActivationWindowViolation =
		Result.RecentActivationCount >= Config.MaximumActivationsPerWindow;
	Result.bValid = !Result.bSameAbilityIntervalViolation &&
		!Result.bActivationWindowViolation;
	return Result;
}

FRPGCombatHitSecurityResult FRPGSecurityValidationMath::ValidateCombatHit(
	const FRPGCombatHitSecuritySample& Sample)
{
	FRPGCombatHitSecurityResult Result;
	if (!RPGSecurityValidation::IsFiniteVector(Sample.SourceLocation) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.TargetBoundsOrigin) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.TargetBoundsExtent) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.ImpactPoint))
	{
		Result.bValid = false;
		Result.Failure =
			ERPGCombatHitValidationFailure::InvalidSpatialData;
		return Result;
	}
	if (!FMath::IsFinite(Sample.MaximumDistance) ||
		Sample.MaximumDistance <= 0.0f ||
		!FMath::IsFinite(Sample.HitLocationTolerance) ||
		Sample.HitLocationTolerance < 0.0f)
	{
		Result.bValid = false;
		Result.Failure = ERPGCombatHitValidationFailure::InvalidLimits;
		return Result;
	}

	const FVector BoundsExtent = Sample.TargetBoundsExtent.GetAbs();
	Result.SourceDistance = FVector::Distance(
		Sample.SourceLocation,
		Sample.TargetBoundsOrigin);
	Result.AllowedDistance =
		Sample.MaximumDistance + BoundsExtent.Size();
	if (!FMath::IsFinite(Result.SourceDistance) ||
		!FMath::IsFinite(Result.AllowedDistance))
	{
		Result.bValid = false;
		Result.Failure =
			ERPGCombatHitValidationFailure::InvalidSpatialData;
		return Result;
	}
	if (Result.SourceDistance > Result.AllowedDistance)
	{
		Result.bValid = false;
		Result.Failure = ERPGCombatHitValidationFailure::RangeExceeded;
		return Result;
	}

	const FVector BoundsMin = Sample.TargetBoundsOrigin - BoundsExtent;
	const FVector BoundsMax = Sample.TargetBoundsOrigin + BoundsExtent;
	const FVector ClosestPoint(
		FMath::Clamp(Sample.ImpactPoint.X, BoundsMin.X, BoundsMax.X),
		FMath::Clamp(Sample.ImpactPoint.Y, BoundsMin.Y, BoundsMax.Y),
		FMath::Clamp(Sample.ImpactPoint.Z, BoundsMin.Z, BoundsMax.Z));
	Result.ImpactPointError = FVector::Distance(
		ClosestPoint,
		Sample.ImpactPoint);
	if (!FMath::IsFinite(Result.ImpactPointError) ||
		Result.ImpactPointError > Sample.HitLocationTolerance)
	{
		Result.bValid = false;
		Result.Failure =
			ERPGCombatHitValidationFailure::ImpactPointOutsideTarget;
	}
	return Result;
}

bool FRPGSecurityValidationMath::IsDamageMagnitudeValid(
	const float Damage,
	const float MaximumDamageMagnitude)
{
	return FMath::IsFinite(Damage) && Damage > 0.0f &&
		FMath::IsFinite(MaximumDamageMagnitude) &&
		MaximumDamageMagnitude > 0.0f &&
		Damage <= MaximumDamageMagnitude;
}

FRPGTargetDataSecurityResult FRPGSecurityValidationMath::ValidateTargetData(
	const FRPGTargetDataSecuritySample& Sample)
{
	FRPGTargetDataSecurityResult Result;
	if (!RPGSecurityValidation::IsFiniteVector(Sample.ServerSourceLocation) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.SubmittedSourceLocation) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.SubmittedTargetLocation) ||
		!RPGSecurityValidation::IsFiniteVector(Sample.SubmittedAimDirection) ||
		(Sample.bValidateAim &&
			!RPGSecurityValidation::IsFiniteVector(Sample.ServerAimDirection)))
	{
		Result.bValid = false;
		Result.bInvalidSpatialData = true;
		return Result;
	}
	if (!FMath::IsFinite(Sample.MaximumRange) ||
		Sample.MaximumRange <= 0.0f ||
		!FMath::IsFinite(Sample.SourceLocationTolerance) ||
		Sample.SourceLocationTolerance < 0.0f ||
		!FMath::IsFinite(Sample.RangeTolerance) ||
		Sample.RangeTolerance < 0.0f ||
		!FMath::IsFinite(Sample.AimToleranceDegrees) ||
		Sample.AimToleranceDegrees < 0.0f ||
		Sample.AimToleranceDegrees > 180.0f)
	{
		Result.bValid = false;
		Result.bInvalidLimits = true;
		return Result;
	}

	Result.SourceLocationError = FVector::Distance(
		Sample.ServerSourceLocation,
		Sample.SubmittedSourceLocation);
	if (Result.SourceLocationError > Sample.SourceLocationTolerance)
	{
		Result.bValid = false;
		Result.bSourceLocationViolation = true;
		return Result;
	}

	FVector ToTarget =
		Sample.SubmittedTargetLocation - Sample.ServerSourceLocation;
	const float AllowedRange =
		Sample.MaximumRange + Sample.RangeTolerance;
	if (Sample.bFlattenAim && FMath::Abs(ToTarget.Z) > AllowedRange)
	{
		Result.bValid = false;
		Result.bVerticalRangeViolation = true;
		return Result;
	}
	if (Sample.bFlattenAim)
	{
		ToTarget.Z = 0.0f;
	}
	Result.TargetDistance = ToTarget.Size();
	if (!FMath::IsFinite(Result.TargetDistance) ||
		Result.TargetDistance > AllowedRange)
	{
		Result.bValid = false;
		Result.bRangeViolation = true;
		return Result;
	}

	Result.ValidatedAimDirection = ToTarget.GetSafeNormal();
	if (Result.ValidatedAimDirection.IsNearlyZero())
	{
		Result.ValidatedAimDirection =
			Sample.SubmittedAimDirection.GetSafeNormal();
		if (Sample.bFlattenAim)
		{
			Result.ValidatedAimDirection.Z = 0.0f;
			Result.ValidatedAimDirection.Normalize();
		}
	}
	if (Result.ValidatedAimDirection.IsNearlyZero())
	{
		Result.bValid = false;
		Result.bAimViolation = true;
		return Result;
	}

	if (Sample.bValidateAim)
	{
		FVector ServerAimDirection = Sample.ServerAimDirection;
		if (Sample.bFlattenAim)
		{
			ServerAimDirection.Z = 0.0f;
		}
		ServerAimDirection = ServerAimDirection.GetSafeNormal();
		if (ServerAimDirection.IsNearlyZero())
		{
			Result.bValid = false;
			Result.bAimViolation = true;
			return Result;
		}

		const float AimDot = FMath::Clamp(
			FVector::DotProduct(
				ServerAimDirection,
				Result.ValidatedAimDirection),
			-1.0f,
			1.0f);
		Result.AimErrorDegrees =
			FMath::RadiansToDegrees(FMath::Acos(AimDot));
		if (!FMath::IsFinite(Result.AimErrorDegrees) ||
			Result.AimErrorDegrees > Sample.AimToleranceDegrees)
		{
			Result.bValid = false;
			Result.bAimViolation = true;
		}
	}
	return Result;
}
