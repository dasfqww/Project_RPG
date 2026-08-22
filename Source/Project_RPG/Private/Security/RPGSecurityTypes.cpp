#include "Security/RPGSecurityTypes.h"

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
