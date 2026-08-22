#include "Skill/RPGSkillTargetingPolicy.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Engine/World.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "StructUtils/InstancedStruct.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillTargetingPolicy)

namespace RPGSkillTargeting
{
	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	bool IsNetworkValidationConfigValid(
		const FRPGSkillTargetingConfig& Config)
	{
		return FMath::IsFinite(Config.ServerSourceLocationTolerance) &&
			Config.ServerSourceLocationTolerance >= 0.0f &&
			FMath::IsFinite(Config.ServerRangeTolerance) &&
			Config.ServerRangeTolerance >= 0.0f &&
			FMath::IsFinite(Config.ServerAimToleranceDegrees) &&
			Config.ServerAimToleranceDegrees >= 0.0f &&
			Config.ServerAimToleranceDegrees <= 180.0f;
	}

	bool FailValidation(FText& OutError, const TCHAR* Message)
	{
		OutError = FText::FromString(Message);
		return false;
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return !Value.ContainsNaN() &&
			FMath::IsFinite(Value.X) &&
			FMath::IsFinite(Value.Y) &&
			FMath::IsFinite(Value.Z);
	}

	bool ValidateCommonReplicatedTarget(
		const IRPGSkillTargetingHost& Host,
		const FRPGSkillTargetingConfig& Config,
		const FRPGSkillTargetResult& SubmittedResult,
		const float MaxRange,
		const bool bFlattenAim,
		const float AdditionalAimToleranceDegrees,
		FRPGSkillTargetResult& OutResult,
		FText& OutError)
	{
		OutResult.Reset();
		AActor* SourceActor = Host.GetSkillSourceActor();
		if (!SubmittedResult.bIsValid || !IsValid(SourceActor) ||
			!IsFiniteVector(SubmittedResult.SourceLocation) ||
			!IsFiniteVector(SubmittedResult.TargetLocation) ||
			!IsFiniteVector(SubmittedResult.AimDirection))
		{
			return FailValidation(
				OutError,
				TEXT("Replicated target contains invalid spatial data."));
		}
		if (SubmittedResult.TargetActor &&
			!IsValid(SubmittedResult.TargetActor))
		{
			return FailValidation(
				OutError,
				TEXT("Replicated target actor is no longer valid."));
		}

		const FVector SourceLocation = SourceActor->GetActorLocation();
		if (FVector::DistSquared(SourceLocation, SubmittedResult.SourceLocation) >
			FMath::Square(Config.ServerSourceLocationTolerance))
		{
			return FailValidation(
				OutError,
				TEXT("Replicated source location exceeds the server tolerance."));
		}

		FVector ToTarget = SubmittedResult.TargetLocation - SourceLocation;
		if (bFlattenAim)
		{
			if (FMath::Abs(ToTarget.Z) >
				MaxRange + Config.ServerRangeTolerance)
			{
				return FailValidation(
					OutError,
					TEXT("Replicated target exceeds the vertical server tolerance."));
			}
			ToTarget.Z = 0.0f;
		}
		const float Distance = ToTarget.Size();
		if (!FMath::IsFinite(Distance) ||
			Distance > MaxRange + Config.ServerRangeTolerance)
		{
			return FailValidation(
				OutError,
				TEXT("Replicated target exceeds the authored range."));
		}

		FVector AimDirection = ToTarget.GetSafeNormal();
		if (AimDirection.IsNearlyZero())
		{
			AimDirection = SubmittedResult.AimDirection.GetSafeNormal();
			if (bFlattenAim)
			{
				AimDirection.Z = 0.0f;
				AimDirection.Normalize();
			}
		}
		if (AimDirection.IsNearlyZero())
		{
			return FailValidation(
				OutError,
				TEXT("Replicated target has no usable aim direction."));
		}

		const bool bMatchesServerLock =
			SubmittedResult.TargetActor &&
			SubmittedResult.TargetActor == Host.GetSkillLockedTarget();
		if (!bMatchesServerLock)
		{
			FVector ViewOrigin;
			FVector ServerAimDirection;
			if (!Host.GetSkillCameraAimRay(ViewOrigin, ServerAimDirection))
			{
				return FailValidation(
					OutError,
					TEXT("Server could not resolve the owning client's control aim."));
			}
			if (bFlattenAim)
			{
				ServerAimDirection.Z = 0.0f;
			}
			ServerAimDirection = ServerAimDirection.GetSafeNormal();
			const float AllowedAngle = FMath::Clamp(
				Config.ServerAimToleranceDegrees +
					AdditionalAimToleranceDegrees,
				0.0f,
				180.0f);
			const float AimDot = FMath::Clamp(
				FVector::DotProduct(ServerAimDirection, AimDirection),
				-1.0f,
				1.0f);
			const float AimErrorDegrees =
				FMath::RadiansToDegrees(FMath::Acos(AimDot));
			if (ServerAimDirection.IsNearlyZero() ||
				AimErrorDegrees > AllowedAngle)
			{
				return FailValidation(
					OutError,
					TEXT("Replicated aim diverges from the server-known control aim."));
			}
		}

		OutResult.bIsValid = true;
		OutResult.TargetActor = SubmittedResult.TargetActor;
		OutResult.SourceLocation = SourceLocation;
		OutResult.TargetLocation = SubmittedResult.TargetLocation;
		OutResult.AimDirection = AimDirection;
		OutResult.bOrientSourceToAim = Config.bOrientSourceToAim;
		OutError = FText::GetEmpty();
		return true;
	}

	bool ValidateTraceToSubmittedPoint(
		UWorld& World,
		AActor& SourceActor,
		const FVector& TraceStart,
		const FVector& SubmittedTarget,
		const ECollisionChannel TraceChannel,
		const float PointTolerance,
		const bool bRequireBlockingHit,
		AActor* SubmittedTargetActor,
		FVector& OutTargetLocation,
		FHitResult& OutHit,
		FText& OutError)
	{
		FCollisionQueryParams Params(
			SCENE_QUERY_STAT(RPGSkillReplicatedTargetValidation), false);
		Params.AddIgnoredActor(&SourceActor);
		const bool bHit = World.LineTraceSingleByChannel(
			OutHit,
			TraceStart,
			SubmittedTarget,
			TraceChannel,
			Params);
		if (!bHit)
		{
			if (bRequireBlockingHit)
			{
				return FailValidation(
					OutError,
					TEXT("Replicated target requires a server blocking hit."));
			}
			OutTargetLocation = SubmittedTarget;
			return true;
		}

		const bool bHitSubmittedActor =
			SubmittedTargetActor && OutHit.GetActor() == SubmittedTargetActor;
		if (SubmittedTargetActor && !bHitSubmittedActor)
		{
			return FailValidation(
				OutError,
				TEXT("A server-side obstruction blocks the replicated target actor."));
		}
		if (!SubmittedTargetActor &&
			FVector::DistSquared(OutHit.ImpactPoint, SubmittedTarget) >
				FMath::Square(PointTolerance))
		{
			return FailValidation(
				OutError,
				TEXT("A server-side obstruction blocks the replicated target."));
		}

		OutTargetLocation = bHitSubmittedActor
			? SubmittedTarget
			: OutHit.ImpactPoint;
		return true;
	}

	FCollisionObjectQueryParams MakeObjectQueryParams(
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes)
	{
		FCollisionObjectQueryParams Result;
		for (const TEnumAsByte<EObjectTypeQuery> ObjectType : ObjectTypes)
		{
			const ECollisionChannel Channel =
				UEngineTypes::ConvertToCollisionChannel(ObjectType.GetValue());
			if (Channel != ECC_OverlapAll_Deprecated)
			{
				Result.AddObjectTypesToQuery(Channel);
			}
		}
		return Result;
	}

	bool ResolveCameraDirection(
		const IRPGSkillTargetingHost& Host,
		const float MaxRange,
		const ECollisionChannel TraceChannel,
		const bool bUseLockedTargetFirst,
		const bool bFlattenAimDirection,
		const bool bRequireBlockingHit,
		const bool bOrientSourceToAim,
		FRPGSkillTargetResult& OutResult)
	{
		OutResult.Reset();
		UWorld* World = Host.GetSkillTargetingWorld();
		AActor* SourceActor = Host.GetSkillSourceActor();
		FVector ViewOrigin;
		FVector ViewDirection;
		if (!World || !IsValid(SourceActor) ||
			!Host.GetSkillCameraAimRay(ViewOrigin, ViewDirection))
		{
			return false;
		}

		ViewDirection = ViewDirection.GetSafeNormal();
		if (ViewDirection.IsNearlyZero())
		{
			return false;
		}

		const FVector SourceLocation = SourceActor->GetActorLocation();
		FCollisionQueryParams QueryParams(
			SCENE_QUERY_STAT(RPGSkillCameraTargeting),
			false);
		QueryParams.AddIgnoredActor(SourceActor);

		FVector TargetLocation = FVector::ZeroVector;
		AActor* TargetActor = nullptr;
		FHitResult AimHit;
		bool bBlockingHit = false;

		if (bUseLockedTargetFirst)
		{
			AActor* LockedTarget = Host.GetSkillLockedTarget();
			if (IsValid(LockedTarget) && LockedTarget != SourceActor &&
				FVector::DistSquared(SourceLocation, LockedTarget->GetActorLocation()) <=
					FMath::Square(MaxRange))
			{
				const FVector LockedLocation = LockedTarget->GetActorLocation();
				const bool bHit = World->LineTraceSingleByChannel(
					AimHit,
					ViewOrigin,
					LockedLocation,
					TraceChannel,
					QueryParams);
				if (!bHit || AimHit.GetActor() == LockedTarget)
				{
					TargetActor = LockedTarget;
					TargetLocation = LockedLocation;
					bBlockingHit = bHit;
				}
			}
		}

		if (!TargetActor)
		{
			const FVector TraceEnd = ViewOrigin + ViewDirection * MaxRange;
			bBlockingHit = World->LineTraceSingleByChannel(
				AimHit,
				ViewOrigin,
				TraceEnd,
				TraceChannel,
				QueryParams);
			TargetLocation =
				bBlockingHit ? AimHit.ImpactPoint : TraceEnd;
			TargetActor = bBlockingHit ? AimHit.GetActor() : nullptr;
		}

		if (bRequireBlockingHit && !bBlockingHit)
		{
			return false;
		}

		FVector AimDirection = TargetLocation - SourceLocation;
		if (bFlattenAimDirection)
		{
			AimDirection.Z = 0.0f;
			TargetLocation.Z = SourceLocation.Z;
		}
		AimDirection = AimDirection.GetSafeNormal();
		if (AimDirection.IsNearlyZero())
		{
			AimDirection = ViewDirection;
			if (bFlattenAimDirection)
			{
				AimDirection.Z = 0.0f;
				AimDirection.Normalize();
			}
		}
		if (AimDirection.IsNearlyZero())
		{
			return false;
		}

		OutResult.bIsValid = true;
		OutResult.TargetActor = TargetActor;
		OutResult.SourceLocation = SourceLocation;
		OutResult.TargetLocation = TargetLocation;
		OutResult.AimDirection = AimDirection;
		OutResult.HitQueryTransform =
			FTransform(AimDirection.Rotation(), SourceLocation);
		OutResult.AimHit = AimHit;
		OutResult.bOrientSourceToAim = bOrientSourceToAim;
		return true;
	}
}

void URPGSkillTargetingPolicy::Initialize(
	IRPGSkillTargetingHost& InHost,
	const FInstancedStruct& InConfig)
{
	TargetingHost = &InHost;
	TargetingConfig = &InConfig;
}

bool URPGSkillTargetingPolicy::ValidateTargetingConfig(
	const FInstancedStruct& ConfigToValidate,
	FText& OutError) const
{
	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillTargetingPolicy::ResolveTarget(
	FRPGSkillTargetResult& OutResult) const
{
	OutResult.Reset();
	return false;
}

bool URPGSkillTargetingPolicy::ValidateReplicatedTarget(
	const FRPGSkillTargetResult& SubmittedResult,
	FRPGSkillTargetResult& OutValidatedResult,
	FText& OutError) const
{
	OutValidatedResult.Reset();
	OutError = FText::FromString(
		TEXT("This targeting policy does not support replicated target validation."));
	return false;
}

const FInstancedStruct& URPGSkillTargetingPolicy::GetConfig() const
{
	check(TargetingConfig);
	return *TargetingConfig;
}

UWorld* URPGSkillTargetingPolicy::GetWorld() const
{
	return TargetingHost
		? TargetingHost->GetSkillTargetingWorld()
		: nullptr;
}

bool URPGSkillTargetingPolicy_CameraDirection::ValidateTargetingConfig(
	const FInstancedStruct& ConfigToValidate,
	FText& OutError) const
{
	const FRPGSkillCameraDirectionTargetingConfig* CameraConfig =
		ConfigToValidate.GetPtr<FRPGSkillCameraDirectionTargetingConfig>();
	if (!CameraConfig ||
		!RPGSkillTargeting::IsFinitePositive(CameraConfig->MaxRange) ||
		!RPGSkillTargeting::IsNetworkValidationConfigValid(*CameraConfig))
	{
		OutError = FText::FromString(
			TEXT("Camera Direction policy requires a valid Camera Direction config."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillTargetingPolicy_CameraDirection::ResolveTarget(
	FRPGSkillTargetResult& OutResult) const
{
	const FRPGSkillCameraDirectionTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillCameraDirectionTargetingConfig>();
	return PolicyConfig && GetHost() &&
		RPGSkillTargeting::ResolveCameraDirection(
			*GetHost(),
			PolicyConfig->MaxRange,
			PolicyConfig->TraceChannel,
			PolicyConfig->bUseLockedTargetFirst,
			PolicyConfig->bFlattenAimDirection,
			PolicyConfig->bRequireBlockingHit,
			PolicyConfig->bOrientSourceToAim,
			OutResult);
}

bool URPGSkillTargetingPolicy_CameraDirection::ValidateReplicatedTarget(
	const FRPGSkillTargetResult& SubmittedResult,
	FRPGSkillTargetResult& OutValidatedResult,
	FText& OutError) const
{
	const FRPGSkillCameraDirectionTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillCameraDirectionTargetingConfig>();
	IRPGSkillTargetingHost* PolicyHost = GetHost();
	if (!PolicyConfig || !PolicyHost ||
		!RPGSkillTargeting::ValidateCommonReplicatedTarget(
			*PolicyHost,
			*PolicyConfig,
			SubmittedResult,
			PolicyConfig->MaxRange,
			PolicyConfig->bFlattenAimDirection,
			0.0f,
			OutValidatedResult,
			OutError))
	{
		return false;
	}

	UWorld* World = GetWorld();
	AActor* SourceActor = PolicyHost->GetSkillSourceActor();
	if (!World || !IsValid(SourceActor))
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Server targeting world is unavailable."));
	}

	if (IsValid(OutValidatedResult.TargetActor) &&
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(
			OutValidatedResult.TargetActor))
	{
		const URPGHitQuerySubsystem* HitQuerySubsystem =
			World->GetSubsystem<URPGHitQuerySubsystem>();
		if (!HitQuerySubsystem ||
			!HitQuerySubsystem->IsTargetEligible(
				SourceActor,
				OutValidatedResult.TargetActor,
				SourceActor->GetActorLocation(),
				PolicyHost->GetSkillTargetValidationFilter()))
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Replicated target actor fails team, tag, or line-of-sight rules."));
		}
	}

	FVector TraceStart;
	FVector IgnoredViewDirection;
	if (!PolicyHost->GetSkillCameraAimRay(
		TraceStart, IgnoredViewDirection))
	{
		FRotator EyeRotation;
		SourceActor->GetActorEyesViewPoint(TraceStart, EyeRotation);
	}
	if (PolicyConfig->bFlattenAimDirection)
	{
		FRotator EyeRotation;
		SourceActor->GetActorEyesViewPoint(TraceStart, EyeRotation);
	}
	FVector TraceTarget = OutValidatedResult.TargetLocation;
	if (PolicyConfig->bFlattenAimDirection)
	{
		TraceTarget.Z = TraceStart.Z;
	}

	FVector ValidatedTargetLocation;
	FHitResult ValidatedHit;
	if (!RPGSkillTargeting::ValidateTraceToSubmittedPoint(
		*World,
		*SourceActor,
		TraceStart,
		TraceTarget,
		PolicyConfig->TraceChannel,
		PolicyConfig->ServerRangeTolerance,
		PolicyConfig->bRequireBlockingHit,
		OutValidatedResult.TargetActor,
		ValidatedTargetLocation,
		ValidatedHit,
		OutError))
	{
		return false;
	}

	OutValidatedResult.TargetLocation = ValidatedTargetLocation;
	FVector AimDirection =
		ValidatedTargetLocation - OutValidatedResult.SourceLocation;
	if (PolicyConfig->bFlattenAimDirection)
	{
		AimDirection.Z = 0.0f;
		OutValidatedResult.TargetLocation.Z =
			OutValidatedResult.SourceLocation.Z;
	}
	OutValidatedResult.AimDirection = AimDirection.GetSafeNormal();
	if (OutValidatedResult.AimDirection.IsNearlyZero())
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Validated camera target has no aim direction."));
	}
	OutValidatedResult.HitQueryTransform = FTransform(
		OutValidatedResult.AimDirection.Rotation(),
		OutValidatedResult.SourceLocation);
	OutValidatedResult.AimHit = ValidatedHit;
	return true;
}

bool URPGSkillTargetingPolicy_SoftTarget::ValidateTargetingConfig(
	const FInstancedStruct& ConfigToValidate,
	FText& OutError) const
{
	const FRPGSkillSoftTargetingConfig* SoftTargetConfig =
		ConfigToValidate.GetPtr<FRPGSkillSoftTargetingConfig>();
	const bool bValid = SoftTargetConfig &&
		RPGSkillTargeting::IsFinitePositive(SoftTargetConfig->MaxRange) &&
		RPGSkillTargeting::IsFinitePositive(
			SoftTargetConfig->AssistAngleDegrees) &&
		SoftTargetConfig->AssistAngleDegrees <= 180.0f &&
		FMath::IsFinite(
			SoftTargetConfig->ServerAssistConeToleranceDegrees) &&
		SoftTargetConfig->ServerAssistConeToleranceDegrees >= 0.0f &&
		SoftTargetConfig->ServerAssistConeToleranceDegrees <= 45.0f &&
		FMath::IsFinite(SoftTargetConfig->VerticalHalfHeight) &&
		SoftTargetConfig->VerticalHalfHeight >= 0.0f &&
		FMath::IsFinite(SoftTargetConfig->AngleScoreWeight) &&
		SoftTargetConfig->AngleScoreWeight >= 0.0f &&
		FMath::IsFinite(SoftTargetConfig->DistanceScoreWeight) &&
		SoftTargetConfig->DistanceScoreWeight >= 0.0f &&
		SoftTargetConfig->AngleScoreWeight +
			SoftTargetConfig->DistanceScoreWeight > KINDA_SMALL_NUMBER &&
		RPGSkillTargeting::IsNetworkValidationConfigValid(*SoftTargetConfig);
	if (!bValid)
	{
		OutError = FText::FromString(
			TEXT("Soft Target policy requires valid range, cone, and score weights."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillTargetingPolicy_SoftTarget::ResolveTarget(
	FRPGSkillTargetResult& OutResult) const
{
	OutResult.Reset();
	const FRPGSkillSoftTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillSoftTargetingConfig>();
	IRPGSkillTargetingHost* PolicyHost = GetHost();
	UWorld* World = GetWorld();
	AActor* SourceActor =
		PolicyHost ? PolicyHost->GetSkillSourceActor() : nullptr;
	FVector ViewOrigin;
	FVector ViewDirection;
	if (!PolicyConfig
		|| !PolicyHost
		|| !World
		|| !IsValid(SourceActor)
		|| !PolicyHost->GetSkillCameraAimRay(ViewOrigin, ViewDirection))
	{
		return false;
	}

	ViewDirection.Z = 0.0f;
	ViewDirection = ViewDirection.GetSafeNormal();
	if (ViewDirection.IsNearlyZero())
	{
		ViewDirection = SourceActor->GetActorForwardVector().GetSafeNormal2D();
	}

	FRPGHitQueryContext QueryContext;
	QueryContext.SourceActor = SourceActor;
	QueryContext.QueryTransform = FTransform(
		ViewDirection.Rotation(),
		SourceActor->GetActorLocation());
	QueryContext.Profile.Shape.Type = ERPGHitQueryShape::Sector;
	QueryContext.Profile.Shape.Radius = PolicyConfig->MaxRange;
	QueryContext.Profile.Shape.HalfHeight =
		PolicyConfig->VerticalHalfHeight;
	QueryContext.Profile.Shape.AngleDegrees =
		FMath::Min(PolicyConfig->AssistAngleDegrees * 2.0f, 360.0f);
	QueryContext.Profile.Filter = PolicyConfig->CandidateFilter;
	QueryContext.Profile.Filter.SortMode = ERPGHitQuerySortMode::None;
	QueryContext.Profile.Filter.MaxResults = 0;

	TArray<FRPGHitQueryResult> Candidates;
	if (const URPGHitQuerySubsystem* HitQuerySubsystem =
		World->GetSubsystem<URPGHitQuerySubsystem>())
	{
		HitQuerySubsystem->ExecuteHitQuery(QueryContext, Candidates);
	}

	const FRPGHitQueryResult* BestCandidate = nullptr;
	if (PolicyConfig->bPreferLockedTarget)
	{
		const AActor* LockedTarget = PolicyHost->GetSkillLockedTarget();
		BestCandidate = Candidates.FindByPredicate(
			[LockedTarget](const FRPGHitQueryResult& Candidate)
			{
				return Candidate.TargetActor == LockedTarget;
			});
	}

	if (!BestCandidate)
	{
		float BestScore = -1.0f;
		for (const FRPGHitQueryResult& Candidate : Candidates)
		{
			const FVector ToCandidate =
				Candidate.QueryPoint - SourceActor->GetActorLocation();
			const FVector Direction2D = ToCandidate.GetSafeNormal2D();
			if (Direction2D.IsNearlyZero())
			{
				continue;
			}

			const float Dot = FMath::Clamp(
				FVector::DotProduct(ViewDirection, Direction2D),
				-1.0f,
				1.0f);
			const float AngleDegrees =
				FMath::RadiansToDegrees(FMath::Acos(Dot));
			const float Score =
				FRPGSkillTargetingMath::CalculateSoftTargetScore(
					AngleDegrees,
					Candidate.Distance,
					PolicyConfig->AssistAngleDegrees,
					PolicyConfig->MaxRange,
					PolicyConfig->AngleScoreWeight,
					PolicyConfig->DistanceScoreWeight);
			if (!BestCandidate || Score > BestScore + KINDA_SMALL_NUMBER ||
				(FMath::IsNearlyEqual(Score, BestScore) &&
					GetNameSafe(Candidate.TargetActor).Compare(
						GetNameSafe(BestCandidate->TargetActor),
						ESearchCase::CaseSensitive) < 0))
			{
				BestCandidate = &Candidate;
				BestScore = Score;
			}
		}
	}

	if (!BestCandidate)
	{
		return PolicyConfig->bFallbackToCameraDirection &&
			RPGSkillTargeting::ResolveCameraDirection(
				*PolicyHost,
				PolicyConfig->MaxRange,
				PolicyConfig->FallbackTraceChannel,
				false,
				true,
				false,
				PolicyConfig->bOrientSourceToAim,
				OutResult);
	}

	const FVector SourceLocation = SourceActor->GetActorLocation();
	FVector AimDirection =
		(BestCandidate->QueryPoint - SourceLocation).GetSafeNormal2D();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection = ViewDirection;
	}

	OutResult.bIsValid = true;
	OutResult.TargetActor = BestCandidate->TargetActor;
	OutResult.SourceLocation = SourceLocation;
	OutResult.TargetLocation = BestCandidate->QueryPoint;
	OutResult.AimDirection = AimDirection;
	OutResult.HitQueryTransform =
		FTransform(AimDirection.Rotation(), SourceLocation);
	OutResult.AimHit = BestCandidate->HitResult;
	OutResult.bOrientSourceToAim =
		PolicyConfig->bOrientSourceToAim;
	return true;
}

bool URPGSkillTargetingPolicy_SoftTarget::ValidateReplicatedTarget(
	const FRPGSkillTargetResult& SubmittedResult,
	FRPGSkillTargetResult& OutValidatedResult,
	FText& OutError) const
{
	const FRPGSkillSoftTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillSoftTargetingConfig>();
	IRPGSkillTargetingHost* PolicyHost = GetHost();
	if (!PolicyConfig || !PolicyHost ||
		!RPGSkillTargeting::ValidateCommonReplicatedTarget(
			*PolicyHost,
			*PolicyConfig,
			SubmittedResult,
			PolicyConfig->MaxRange,
			true,
			0.0f,
			OutValidatedResult,
			OutError))
	{
		return false;
	}

	UWorld* World = GetWorld();
	AActor* SourceActor = PolicyHost->GetSkillSourceActor();
	const URPGHitQuerySubsystem* HitQuerySubsystem =
		World ? World->GetSubsystem<URPGHitQuerySubsystem>() : nullptr;
	if (!World || !IsValid(SourceActor) || !HitQuerySubsystem)
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Server soft-target query service is unavailable."));
	}

	if (IsValid(OutValidatedResult.TargetActor))
	{
		FVector ServerViewOrigin;
		FVector ServerViewDirection;
		if (!PolicyHost->GetSkillCameraAimRay(
			ServerViewOrigin,
			ServerViewDirection))
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Server could not resolve the soft-target camera."));
		}
		ServerViewDirection.Z = 0.0f;
		ServerViewDirection = ServerViewDirection.GetSafeNormal();
		if (ServerViewDirection.IsNearlyZero())
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Server soft-target camera has no planar direction."));
		}

		FRPGHitQueryContext QueryContext;
		QueryContext.SourceActor = SourceActor;
		QueryContext.QueryTransform = FTransform(
			ServerViewDirection.Rotation(),
			OutValidatedResult.SourceLocation);
		QueryContext.Profile.Shape.Type = ERPGHitQueryShape::Sector;
		QueryContext.Profile.Shape.Radius =
			PolicyConfig->MaxRange + PolicyConfig->ServerRangeTolerance;
		QueryContext.Profile.Shape.HalfHeight =
			PolicyConfig->VerticalHalfHeight +
				PolicyConfig->ServerRangeTolerance;
		QueryContext.Profile.Shape.AngleDegrees = FMath::Min(
			(PolicyConfig->AssistAngleDegrees +
			 PolicyConfig->ServerAssistConeToleranceDegrees) * 2.0f,
			360.0f);
		QueryContext.Profile.Filter = PolicyConfig->CandidateFilter;
		QueryContext.Profile.Filter.SortMode = ERPGHitQuerySortMode::None;
		QueryContext.Profile.Filter.MaxResults = 0;

		TArray<FRPGHitQueryResult> Candidates;
		HitQuerySubsystem->ExecuteHitQuery(QueryContext, Candidates);
		const FRPGHitQueryResult* AuthoritativeCandidate =
			Candidates.FindByPredicate(
				[TargetActor = OutValidatedResult.TargetActor](
					const FRPGHitQueryResult& Candidate)
				{
					return Candidate.TargetActor == TargetActor;
				});
		if (!AuthoritativeCandidate)
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Replicated soft target fails range, team, tag, or line-of-sight rules."));
		}
		if (FVector::DistSquared(
				AuthoritativeCandidate->QueryPoint,
				SubmittedResult.TargetLocation) >
			FMath::Square(
				PolicyConfig->ServerRangeTolerance + 100.0f))
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Replicated soft-target point does not match the target actor."));
		}

		OutValidatedResult.TargetLocation =
			AuthoritativeCandidate->QueryPoint;
		OutValidatedResult.AimHit = AuthoritativeCandidate->HitResult;
	}
	else
	{
		if (!PolicyConfig->bFallbackToCameraDirection)
		{
			return RPGSkillTargeting::FailValidation(
				OutError,
				TEXT("Soft-target policy requires an eligible target actor."));
		}

		FVector TraceStart;
		FRotator EyeRotation;
		SourceActor->GetActorEyesViewPoint(TraceStart, EyeRotation);
		FVector TraceTarget = OutValidatedResult.TargetLocation;
		TraceTarget.Z = TraceStart.Z;
		FVector ValidatedTargetLocation;
		FHitResult ValidatedHit;
		if (!RPGSkillTargeting::ValidateTraceToSubmittedPoint(
			*World,
			*SourceActor,
			TraceStart,
			TraceTarget,
			PolicyConfig->FallbackTraceChannel,
			PolicyConfig->ServerRangeTolerance,
			false,
			nullptr,
			ValidatedTargetLocation,
			ValidatedHit,
			OutError))
		{
			return false;
		}
		OutValidatedResult.TargetLocation = ValidatedTargetLocation;
		OutValidatedResult.TargetLocation.Z =
			OutValidatedResult.SourceLocation.Z;
		OutValidatedResult.AimHit = ValidatedHit;
	}

	OutValidatedResult.AimDirection =
		(OutValidatedResult.TargetLocation -
			OutValidatedResult.SourceLocation).GetSafeNormal2D();
	if (OutValidatedResult.AimDirection.IsNearlyZero())
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Validated soft target has no planar aim direction."));
	}
	OutValidatedResult.HitQueryTransform = FTransform(
		OutValidatedResult.AimDirection.Rotation(),
		OutValidatedResult.SourceLocation);
	return true;
}

bool URPGSkillTargetingPolicy_GroundPoint::ValidateTargetingConfig(
	const FInstancedStruct& ConfigToValidate,
	FText& OutError) const
{
	const FRPGSkillGroundPointTargetingConfig* GroundConfig =
		ConfigToValidate.GetPtr<FRPGSkillGroundPointTargetingConfig>();
	const bool bValid = GroundConfig &&
		RPGSkillTargeting::IsFinitePositive(GroundConfig->MaxRange) &&
		FMath::IsFinite(GroundConfig->GroundTraceHeight) &&
		GroundConfig->GroundTraceHeight >= 0.0f &&
		RPGSkillTargeting::IsFinitePositive(GroundConfig->GroundTraceDepth) &&
		!GroundConfig->GroundObjectTypes.IsEmpty() &&
		RPGSkillTargeting::MakeObjectQueryParams(
			GroundConfig->GroundObjectTypes).IsValid() &&
		RPGSkillTargeting::IsNetworkValidationConfigValid(*GroundConfig);
	if (!bValid)
	{
		OutError = FText::FromString(
			TEXT("Ground Point policy requires valid range, trace depth, and ground object types."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillTargetingPolicy_GroundPoint::ResolveTarget(
	FRPGSkillTargetResult& OutResult) const
{
	OutResult.Reset();
	const FRPGSkillGroundPointTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillGroundPointTargetingConfig>();
	IRPGSkillTargetingHost* PolicyHost = GetHost();
	UWorld* World = GetWorld();
	AActor* SourceActor =
		PolicyHost ? PolicyHost->GetSkillSourceActor() : nullptr;
	FVector ViewOrigin;
	FVector ViewDirection;
	if (!PolicyConfig
		|| !PolicyHost
		|| !World
		|| !IsValid(SourceActor)
		|| !PolicyHost->GetSkillCameraAimRay(
			ViewOrigin,
			ViewDirection))
	{
		return false;
	}

	ViewDirection = ViewDirection.GetSafeNormal();
	const FVector CameraTraceEnd =
		ViewOrigin + ViewDirection * PolicyConfig->MaxRange;
	FCollisionQueryParams CameraParams(
		SCENE_QUERY_STAT(RPGSkillGroundCameraTrace),
		false);
	CameraParams.AddIgnoredActor(SourceActor);
	FHitResult CameraHit;
	const bool bCameraHit = World->LineTraceSingleByChannel(
		CameraHit,
		ViewOrigin,
		CameraTraceEnd,
		PolicyConfig->CameraTraceChannel,
		CameraParams);
	FVector CandidateLocation =
		bCameraHit ? CameraHit.ImpactPoint : CameraTraceEnd;

	const FVector SourceLocation = SourceActor->GetActorLocation();
	FVector HorizontalOffset = CandidateLocation - SourceLocation;
	HorizontalOffset.Z = 0.0f;
	if (HorizontalOffset.SizeSquared()
		> FMath::Square(PolicyConfig->MaxRange))
	{
		HorizontalOffset =
			HorizontalOffset.GetSafeNormal() * PolicyConfig->MaxRange;
		CandidateLocation.X = SourceLocation.X + HorizontalOffset.X;
		CandidateLocation.Y = SourceLocation.Y + HorizontalOffset.Y;
	}

	const FVector GroundTraceStart =
		CandidateLocation
		+ FVector::UpVector * PolicyConfig->GroundTraceHeight;
	const FVector GroundTraceEnd =
		CandidateLocation
		- FVector::UpVector * PolicyConfig->GroundTraceDepth;
	FCollisionQueryParams GroundParams(
		SCENE_QUERY_STAT(RPGSkillGroundProjection),
		false);
	GroundParams.AddIgnoredActor(SourceActor);
	FHitResult GroundHit;
	const bool bGroundHit = World->LineTraceSingleByObjectType(
		GroundHit,
		GroundTraceStart,
		GroundTraceEnd,
		RPGSkillTargeting::MakeObjectQueryParams(
			PolicyConfig->GroundObjectTypes),
		GroundParams);
	if (!bGroundHit && PolicyConfig->bRequireGroundHit)
	{
		return false;
	}

	const FVector TargetLocation =
		bGroundHit ? GroundHit.ImpactPoint : CandidateLocation;
	FVector AimDirection = (TargetLocation - SourceLocation).GetSafeNormal2D();
	if (AimDirection.IsNearlyZero())
	{
		AimDirection = SourceActor->GetActorForwardVector().GetSafeNormal2D();
	}
	if (AimDirection.IsNearlyZero())
	{
		return false;
	}

	OutResult.bIsValid = true;
	OutResult.TargetActor = bGroundHit ? GroundHit.GetActor() : nullptr;
	OutResult.SourceLocation = SourceLocation;
	OutResult.TargetLocation = TargetLocation;
	OutResult.AimDirection = AimDirection;
	OutResult.HitQueryTransform =
		FTransform(AimDirection.Rotation(), TargetLocation);
	OutResult.AimHit = bGroundHit ? GroundHit : CameraHit;
	OutResult.bOrientSourceToAim =
		PolicyConfig->bOrientSourceToAim;
	return true;
}

bool URPGSkillTargetingPolicy_GroundPoint::ValidateReplicatedTarget(
	const FRPGSkillTargetResult& SubmittedResult,
	FRPGSkillTargetResult& OutValidatedResult,
	FText& OutError) const
{
	const FRPGSkillGroundPointTargetingConfig* PolicyConfig =
		GetConfig().GetPtr<FRPGSkillGroundPointTargetingConfig>();
	IRPGSkillTargetingHost* PolicyHost = GetHost();
	if (!PolicyConfig || !PolicyHost ||
		!RPGSkillTargeting::ValidateCommonReplicatedTarget(
			*PolicyHost,
			*PolicyConfig,
			SubmittedResult,
			PolicyConfig->MaxRange,
			true,
			0.0f,
			OutValidatedResult,
			OutError))
	{
		return false;
	}

	UWorld* World = GetWorld();
	AActor* SourceActor = PolicyHost->GetSkillSourceActor();
	if (!World || !IsValid(SourceActor))
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Server ground-targeting world is unavailable."));
	}

	FVector CameraOrigin;
	FVector CameraDirection;
	if (!PolicyHost->GetSkillCameraAimRay(CameraOrigin, CameraDirection))
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Server could not resolve the ground-targeting camera."));
	}
	FVector CameraValidatedPoint;
	FHitResult CameraHit;
	if (!RPGSkillTargeting::ValidateTraceToSubmittedPoint(
		*World,
		*SourceActor,
		CameraOrigin,
		OutValidatedResult.TargetLocation,
		PolicyConfig->CameraTraceChannel,
		PolicyConfig->ServerRangeTolerance,
		false,
		nullptr,
		CameraValidatedPoint,
		CameraHit,
		OutError))
	{
		return false;
	}

	const FVector GroundTraceStart =
		CameraValidatedPoint +
		FVector::UpVector * PolicyConfig->GroundTraceHeight;
	const FVector GroundTraceEnd =
		CameraValidatedPoint -
		FVector::UpVector * PolicyConfig->GroundTraceDepth;
	FCollisionQueryParams GroundParams(
		SCENE_QUERY_STAT(RPGSkillReplicatedGroundProjection), false);
	GroundParams.AddIgnoredActor(SourceActor);
	FHitResult GroundHit;
	const bool bGroundHit = World->LineTraceSingleByObjectType(
		GroundHit,
		GroundTraceStart,
		GroundTraceEnd,
		RPGSkillTargeting::MakeObjectQueryParams(
			PolicyConfig->GroundObjectTypes),
		GroundParams);
	if (!bGroundHit && PolicyConfig->bRequireGroundHit)
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Replicated ground point does not project onto an allowed object."));
	}

	OutValidatedResult.TargetActor = nullptr;
	OutValidatedResult.TargetLocation =
		bGroundHit ? GroundHit.ImpactPoint : CameraValidatedPoint;
	OutValidatedResult.AimDirection =
		(OutValidatedResult.TargetLocation -
			OutValidatedResult.SourceLocation).GetSafeNormal2D();
	if (OutValidatedResult.AimDirection.IsNearlyZero())
	{
		return RPGSkillTargeting::FailValidation(
			OutError,
			TEXT("Validated ground point has no planar aim direction."));
	}
	OutValidatedResult.HitQueryTransform = FTransform(
		OutValidatedResult.AimDirection.Rotation(),
		OutValidatedResult.TargetLocation);
	OutValidatedResult.AimHit = bGroundHit ? GroundHit : CameraHit;
	return true;
}
