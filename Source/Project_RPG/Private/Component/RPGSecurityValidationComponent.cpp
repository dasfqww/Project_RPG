#include "Component/RPGSecurityValidationComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Security/RPGSecurityPolicy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSecurityValidationComponent)

DEFINE_LOG_CATEGORY_STATIC(LogRPGSecurity, Log, All);

namespace RPGSecurity
{
	constexpr double MinimumLogIntervalSeconds = 1.0;

	FString SanitizeLogDetail(FString Detail)
	{
		Detail.ReplaceInline(TEXT("\r"), TEXT(" "));
		Detail.ReplaceInline(TEXT("\n"), TEXT(" "));
		return Detail.Left(512);
	}
}

URPGSecurityValidationComponent::URPGSecurityValidationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	PrimaryComponentTick.TickInterval = 0.10f;
	SetIsReplicatedByDefault(false);
}

void URPGSecurityValidationComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(IsAuthorityOwner());
	if (!IsAuthorityOwner())
	{
		return;
	}

	PrimaryComponentTick.TickInterval = FMath::Max(
		0.02f,
		GetPolicyConfigRef().Movement.SampleIntervalSeconds);
	SpawnGraceEndsAt =
		GetServerTimeSeconds() + FMath::Max(
			0.0f,
			GetPolicyConfigRef().Movement.SpawnGraceSeconds);
	ResetMovementBaseline();
}

void URPGSecurityValidationComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsAuthorityOwner())
	{
		return;
	}

	DecayRiskScore(DeltaTime);
	if (GetPolicyConfigRef().Movement.bEnabled)
	{
		SampleMovement();
	}
}

bool URPGSecurityValidationComponent::CanAcceptAbilityActivation(
	const UClass* AbilityClass,
	FString& OutReason) const
{
	OutReason.Reset();
	const FRPGAbilitySecurityConfig& AbilityPolicy =
		GetPolicyConfigRef().Ability;
	if (!IsAuthorityOwner() || !AbilityPolicy.bEnabled || !AbilityClass)
	{
		return true;
	}

	const double Now = GetServerTimeSeconds();
	const FName AbilityKey(*AbilityClass->GetPathName());
	if (const double* LastActivation = LastAbilityActivationByClass.Find(AbilityKey);
		LastActivation &&
		Now - *LastActivation < FMath::Max(
			0.0,
			static_cast<double>(
				AbilityPolicy.MinimumSameAbilityIntervalSeconds)))
	{
		OutReason = FString::Printf(
			TEXT("Ability %s was requested faster than the server limit."),
			*AbilityClass->GetName());
		return false;
	}

	const double WindowStart = Now - FMath::Max(
		0.1,
		static_cast<double>(AbilityPolicy.ActivationWindowSeconds));
	int32 RecentActivationCount = 0;
	for (const double Timestamp : AbilityActivationHistory)
	{
		RecentActivationCount += Timestamp >= WindowStart ? 1 : 0;
	}
	if (RecentActivationCount >=
		FMath::Max(1, AbilityPolicy.MaximumActivationsPerWindow))
	{
		OutReason = FString::Printf(
			TEXT("Ability activation window exceeded by %s."),
			*AbilityClass->GetName());
		return false;
	}

	return true;
}

void URPGSecurityValidationComponent::RecordAbilityActivation(
	const UClass* AbilityClass)
{
	if (!IsAuthorityOwner() ||
		!GetPolicyConfigRef().Ability.bEnabled || !AbilityClass)
	{
		return;
	}

	const double Now = GetServerTimeSeconds();
	PruneAbilityActivationHistory(Now);
	AbilityActivationHistory.Add(Now);
	LastAbilityActivationByClass.Add(
		FName(*AbilityClass->GetPathName()),
		Now);
}

bool URPGSecurityValidationComponent::ValidateCombatHit(
	AActor* TargetActor,
	const FHitResult& HitResult,
	const float MaximumDistance,
	const float HitLocationTolerance,
	FString& OutReason)
{
	OutReason.Reset();
	AActor* SourceActor = GetOwner();
	auto Reject = [this, &OutReason](const FString& Reason)
	{
		OutReason = Reason;
		ReportViolation(
			ERPGSecurityViolationType::InvalidCombatHit,
			ERPGSecurityViolationSeverity::High,
			5.0f,
			Reason);
		return false;
	};

	if (!IsAuthorityOwner() || !IsValid(SourceActor) || !IsValid(TargetActor) ||
		SourceActor == TargetActor || SourceActor->GetWorld() != TargetActor->GetWorld())
	{
		return Reject(TEXT("Combat hit has an invalid source or target."));
	}
	if (SourceActor->GetActorLocation().ContainsNaN() ||
		TargetActor->GetActorLocation().ContainsNaN() ||
		HitResult.ImpactPoint.ContainsNaN())
	{
		return Reject(TEXT("Combat hit contains non-finite spatial data."));
	}

	FVector TargetBoundsOrigin = TargetActor->GetActorLocation();
	FVector TargetBoundsExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(
		true,
		TargetBoundsOrigin,
		TargetBoundsExtent,
		false);
	const float TargetAllowance = TargetBoundsExtent.Size();
	const float SourceDistance = FVector::Distance(
		SourceActor->GetActorLocation(),
		TargetBoundsOrigin);
	if (MaximumDistance <= 0.0f ||
		SourceDistance > MaximumDistance + TargetAllowance)
	{
		return Reject(FString::Printf(
			TEXT("Combat hit range %.1f exceeded server allowance %.1f for %s."),
			SourceDistance,
			MaximumDistance + TargetAllowance,
			*GetNameSafe(TargetActor)));
	}

	if (HitResult.GetComponent())
	{
		const FVector BoundsMin = TargetBoundsOrigin - TargetBoundsExtent;
		const FVector BoundsMax = TargetBoundsOrigin + TargetBoundsExtent;
		const FVector ClosestPoint(
			FMath::Clamp(HitResult.ImpactPoint.X, BoundsMin.X, BoundsMax.X),
			FMath::Clamp(HitResult.ImpactPoint.Y, BoundsMin.Y, BoundsMax.Y),
			FMath::Clamp(HitResult.ImpactPoint.Z, BoundsMin.Z, BoundsMax.Z));
		if (FVector::DistSquared(ClosestPoint, HitResult.ImpactPoint) >
			FMath::Square(FMath::Max(0.0f, HitLocationTolerance)))
		{
			return Reject(FString::Printf(
				TEXT("Combat impact point does not intersect %s server bounds."),
				*GetNameSafe(TargetActor)));
		}
	}

	return true;
}

bool URPGSecurityValidationComponent::ValidateDamage(
	const float Damage,
	FString& OutReason)
{
	OutReason.Reset();
	if (!IsAuthorityOwner())
	{
		OutReason = TEXT("Only authority may validate gameplay damage.");
		return false;
	}
	if (!FMath::IsFinite(Damage) || Damage <= 0.0f ||
		Damage > FMath::Max(
			1.0f,
			GetPolicyConfigRef().Combat.MaximumDamageMagnitude))
	{
		OutReason = FString::Printf(
			TEXT("Rejected invalid damage magnitude %.3f."),
			Damage);
		ReportViolation(
			ERPGSecurityViolationType::InvalidDamage,
			ERPGSecurityViolationSeverity::Critical,
			20.0f,
			OutReason);
		return false;
	}
	return true;
}

void URPGSecurityValidationComponent::ReportInvalidTargetData(
	const FString& Detail)
{
	ReportViolation(
		ERPGSecurityViolationType::InvalidTargetData,
		ERPGSecurityViolationSeverity::High,
		5.0f,
		Detail);
}

void URPGSecurityValidationComponent::ReportViolation(
	const ERPGSecurityViolationType Type,
	const ERPGSecurityViolationSeverity Severity,
	const float Score,
	const FString& Detail)
{
	if (!IsAuthorityOwner())
	{
		return;
	}

	FRPGSecurityViolation Violation;
	Violation.Type = Type;
	Violation.Severity = Severity;
	Violation.Score = FMath::Max(0.0f, Score);
	Violation.ServerTimeSeconds = GetServerTimeSeconds();
	Violation.Detail = RPGSecurity::SanitizeLogDetail(Detail);

	++TotalViolationCount;
	RiskScore = FMath::Clamp(RiskScore + Violation.Score, 0.0f, 100000.0f);
	OnViolationReported.Broadcast(Violation);

	const double* LastLogTime = LastLogTimeByType.Find(Type);
	if (GetPolicyConfigRef().Scoring.bLogViolations &&
		(!LastLogTime ||
		 Violation.ServerTimeSeconds - *LastLogTime >=
			 RPGSecurity::MinimumLogIntervalSeconds))
	{
		LastLogTimeByType.Add(Type, Violation.ServerTimeSeconds);
		const UEnum* TypeEnum = StaticEnum<ERPGSecurityViolationType>();
		const UEnum* SeverityEnum = StaticEnum<ERPGSecurityViolationSeverity>();
		UE_LOG(
			LogRPGSecurity,
			Warning,
			TEXT("SecurityViolation Owner=%s Type=%s Severity=%s Score=%.2f Risk=%.2f Detail=\"%s\""),
			*GetNameSafe(GetOwner()),
			TypeEnum ? *TypeEnum->GetNameStringByValue(static_cast<int64>(Type)) : TEXT("Unknown"),
			SeverityEnum ? *SeverityEnum->GetNameStringByValue(static_cast<int64>(Severity)) : TEXT("Unknown"),
			Violation.Score,
			RiskScore,
			*Violation.Detail);
	}

	if (!bRiskThresholdBroadcast &&
		RiskScore >= FMath::Max(
			1.0f,
			GetPolicyConfigRef().Scoring.RiskThreshold))
	{
		bRiskThresholdBroadcast = true;
		OnRiskThresholdExceeded.Broadcast(RiskScore);
	}
}

void URPGSecurityValidationComponent::AuthorizeMovementDiscontinuity(
	const float DurationSeconds,
	const float ExtraDistance,
	const FName Reason)
{
	if (!IsAuthorityOwner())
	{
		return;
	}

	AuthorizedMovementEndsAt = GetServerTimeSeconds() +
		FMath::Clamp(DurationSeconds, 0.0f, 10.0f);
	AuthorizedExtraDistance = FMath::Clamp(ExtraDistance, 0.0f, 100000.0f);
	AuthorizedMovementReason = Reason;
}

void URPGSecurityValidationComponent::CancelMovementAuthorization(
	const FName Reason,
	const bool bResetBaseline)
{
	if (!IsAuthorityOwner() ||
		(!Reason.IsNone() && Reason != AuthorizedMovementReason))
	{
		return;
	}

	AuthorizedMovementEndsAt = 0.0;
	AuthorizedExtraDistance = 0.0f;
	AuthorizedMovementReason = NAME_None;
	if (bResetBaseline)
	{
		ResetMovementBaseline();
	}
}

void URPGSecurityValidationComponent::SetSecurityPolicy(
	URPGSecurityPolicy* NewPolicy)
{
	if (!IsAuthorityOwner())
	{
		return;
	}

	SecurityPolicy = NewPolicy;
	PrimaryComponentTick.TickInterval = FMath::Max(
		0.02f,
		GetPolicyConfigRef().Movement.SampleIntervalSeconds);
	ResetMovementBaseline();
}

FRPGSecurityPolicyConfig
URPGSecurityValidationComponent::GetEffectivePolicyConfig() const
{
	return GetPolicyConfigRef();
}

bool URPGSecurityValidationComponent::IsMovementAuthorizationActive() const
{
	return IsAuthorityOwner() && AuthorizedExtraDistance > 0.0f &&
		GetServerTimeSeconds() <= AuthorizedMovementEndsAt;
}

void URPGSecurityValidationComponent::ResetMovementBaseline()
{
	bHasMovementBaseline = false;
	ConsecutiveSpeedViolationSamples = 0;
	PreviousMovementSample = FRPGMovementSecuritySample();
}

double URPGSecurityValidationComponent::GetServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<double>(World->GetTimeSeconds()) : 0.0;
}

void URPGSecurityValidationComponent::SampleMovement()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* Movement =
		Character ? Character->GetCharacterMovement() : nullptr;
	if (!Character || !Movement || !Character->IsPlayerControlled())
	{
		ResetMovementBaseline();
		return;
	}

	const double Now = GetServerTimeSeconds();
	FRPGMovementSecuritySample Current;
	Current.Location = Character->GetActorLocation();
	Current.Velocity = Movement->Velocity;
	Current.ServerTimeSeconds = Now;
	Current.DeclaredMaximumSpeed = FMath::Max(0.0f, Movement->GetMaxSpeed());
	// Falling is deliberately not exempt: otherwise a speed hack can jump before
	// accelerating. Root motion is server-authored, while exceptional dashes and
	// knockbacks must use AuthorizeMovementDiscontinuity below.
	Current.bSkipHorizontalSpeedCheck = Movement->HasAnimRootMotion();
	if (Now <= AuthorizedMovementEndsAt && AuthorizedExtraDistance > 0.0f)
	{
		Current.AuthorizedExtraDistance = AuthorizedExtraDistance;
		Current.bSkipHorizontalSpeedCheck = true;
	}
	else
	{
		AuthorizedExtraDistance = 0.0f;
		AuthorizedMovementReason = NAME_None;
	}

	if (Now < SpawnGraceEndsAt || !bHasMovementBaseline)
	{
		PreviousMovementSample = Current;
		bHasMovementBaseline = true;
		return;
	}

	const FRPGMovementSecurityResult Result =
		FRPGSecurityValidationMath::ValidateMovement(
			PreviousMovementSample,
			Current,
			GetPolicyConfigRef().Movement);
	PreviousMovementSample = Current;
	if (Result.bValid && Result.AuthorizedDistanceUsed > 0.0f)
	{
		AuthorizedExtraDistance = FMath::Max(
			0.0f,
			AuthorizedExtraDistance - Result.AuthorizedDistanceUsed);
	}

	if (Result.bSampleGapTooLarge)
	{
		ConsecutiveSpeedViolationSamples = 0;
		return;
	}
	if (Result.bValid)
	{
		ConsecutiveSpeedViolationSamples = 0;
		return;
	}

	if (Result.bDiscontinuity)
	{
		ConsecutiveSpeedViolationSamples = 0;
		ReportViolation(
			ERPGSecurityViolationType::MovementDiscontinuity,
			ERPGSecurityViolationSeverity::High,
			10.0f,
			FString::Printf(
				TEXT("Server observed %.1f cm movement in %.3f s; allowance %.1f cm."),
				Result.Distance,
				Result.DeltaSeconds,
				Result.AllowedDistance));
		return;
	}

	if (Result.bSpeedViolation || !Result.bValid)
	{
		++ConsecutiveSpeedViolationSamples;
		if (ConsecutiveSpeedViolationSamples >= FMath::Max(
			1,
			GetPolicyConfigRef().Movement.ConsecutiveSpeedSamplesBeforeReport))
		{
			ConsecutiveSpeedViolationSamples = 0;
			ReportViolation(
				ERPGSecurityViolationType::MovementSpeed,
				ERPGSecurityViolationSeverity::Medium,
				3.0f,
				FString::Printf(
					TEXT("Server observed %.1f cm movement (%.1f cm/s horizontal); allowances %.1f cm and %.1f cm/s."),
					Result.Distance,
					Result.HorizontalSpeed,
					Result.AllowedDistance,
					Result.AllowedWalkingSpeed));
		}
	}
}

void URPGSecurityValidationComponent::DecayRiskScore(const float DeltaTime)
{
	RiskScore = FMath::Max(
		0.0f,
		RiskScore - FMath::Max(
			0.0f,
			GetPolicyConfigRef().Scoring.RiskDecayPerSecond) *
			FMath::Max(0.0f, DeltaTime));
	if (bRiskThresholdBroadcast &&
		RiskScore < FMath::Max(
			1.0f,
			GetPolicyConfigRef().Scoring.RiskThreshold) * 0.5f)
	{
		bRiskThresholdBroadcast = false;
	}
}

void URPGSecurityValidationComponent::PruneAbilityActivationHistory(
	const double Now)
{
	const double WindowStart = Now - FMath::Max(
		0.1,
		static_cast<double>(
			GetPolicyConfigRef().Ability.ActivationWindowSeconds));
	AbilityActivationHistory.RemoveAll(
		[WindowStart](const double Timestamp)
		{
			return Timestamp < WindowStart;
		});
}

bool URPGSecurityValidationComponent::IsAuthorityOwner() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

const FRPGSecurityPolicyConfig&
URPGSecurityValidationComponent::GetPolicyConfigRef() const
{
	return SecurityPolicy ? SecurityPolicy->Config : FallbackPolicy;
}
