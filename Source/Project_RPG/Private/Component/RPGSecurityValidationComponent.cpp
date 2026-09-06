#include "Component/RPGSecurityValidationComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Security/RPGSecurityPolicy.h"
#include "Security/RPGSecurityTelemetrySubsystem.h"

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
	FString EnforcementPolicyError;
	if (!GetPolicyConfigRef().Enforcement.IsValid(&EnforcementPolicyError))
	{
		UE_LOG(
			LogRPGSecurity,
			Error,
			TEXT("Invalid security enforcement policy for %s: %s"),
			*GetNameSafe(GetOwner()),
			*EnforcementPolicyError);
	}
	EvaluateEnforcementState();
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
	FString& OutReason)
{
	OutReason.Reset();
	if (IsAuthorityOwner() && AbilityClass &&
		!CanPerformProtectedAction(
			TEXT("AbilityActivation"),
			true,
			OutReason))
	{
		return false;
	}
	const FRPGAbilitySecurityConfig& AbilityPolicy =
		GetPolicyConfigRef().Ability;
	if (!IsAuthorityOwner() || !AbilityPolicy.bEnabled || !AbilityClass)
	{
		return true;
	}

	const double Now = GetServerTimeSeconds();
	const FName AbilityKey(*AbilityClass->GetPathName());
	const FRPGAbilityActivationSecurityResult Result =
		FRPGSecurityValidationMath::ValidateAbilityActivation(
			AbilityKey,
			Now,
			AbilityActivationHistory,
			LastAbilityActivationByClass,
			AbilityPolicy);
	if (Result.bInvalidServerTime || Result.bInvalidPolicy)
	{
		OutReason = TEXT("Ability security policy or server time is invalid.");
		return false;
	}
	if (Result.bSameAbilityIntervalViolation)
	{
		OutReason = FString::Printf(
			TEXT("Ability %s was requested faster than the server limit."),
			*AbilityClass->GetName());
		ReportViolation(
			ERPGSecurityViolationType::AbilityActivationRate,
			ERPGSecurityViolationSeverity::Medium,
			3.0f,
			OutReason);
		return false;
	}
	if (Result.bActivationWindowViolation)
	{
		OutReason = FString::Printf(
			TEXT("Ability activation window exceeded by %s."),
			*AbilityClass->GetName());
		ReportViolation(
			ERPGSecurityViolationType::AbilityActivationRate,
			ERPGSecurityViolationSeverity::High,
			5.0f,
			OutReason);
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
		SourceActor == TargetActor ||
		HitResult.GetActor() != TargetActor ||
		SourceActor->GetWorld() != TargetActor->GetWorld())
	{
		return Reject(TEXT("Combat hit has an invalid source or target."));
	}

	FVector TargetBoundsOrigin = TargetActor->GetActorLocation();
	FVector TargetBoundsExtent = FVector::ZeroVector;
	TargetActor->GetActorBounds(
		true,
		TargetBoundsOrigin,
		TargetBoundsExtent,
		false);
	FRPGCombatHitSecuritySample Sample;
	Sample.SourceLocation = SourceActor->GetActorLocation();
	Sample.TargetBoundsOrigin = TargetBoundsOrigin;
	Sample.TargetBoundsExtent = TargetBoundsExtent;
	Sample.ImpactPoint = HitResult.ImpactPoint;
	Sample.MaximumDistance = MaximumDistance;
	Sample.HitLocationTolerance = HitLocationTolerance;
	const FRPGCombatHitSecurityResult Result =
		FRPGSecurityValidationMath::ValidateCombatHit(Sample);
	if (!Result.bValid)
	{
		switch (Result.Failure)
		{
		case ERPGCombatHitValidationFailure::RangeExceeded:
			return Reject(FString::Printf(
				TEXT("Combat hit range %.1f exceeded server allowance %.1f for %s."),
				Result.SourceDistance,
				Result.AllowedDistance,
				*GetNameSafe(TargetActor)));
		case ERPGCombatHitValidationFailure::ImpactPointOutsideTarget:
			return Reject(FString::Printf(
				TEXT("Combat impact point missed %s server bounds by %.1f cm."),
				*GetNameSafe(TargetActor),
				Result.ImpactPointError));
		case ERPGCombatHitValidationFailure::InvalidLimits:
			return Reject(TEXT("Combat hit security limits are invalid."));
		case ERPGCombatHitValidationFailure::InvalidSpatialData:
		default:
			return Reject(TEXT("Combat hit contains non-finite spatial data."));
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
	if (!FRPGSecurityValidationMath::IsDamageMagnitudeValid(
		Damage,
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
	Violation.Score = FMath::IsFinite(Score)
		? FMath::Clamp(Score, 0.0f, 100000.0f)
		: 0.0f;
	Violation.ServerTimeSeconds = GetServerTimeSeconds();
	Violation.Detail = RPGSecurity::SanitizeLogDetail(Detail);

	++TotalViolationCount;
	RiskScore = FMath::IsFinite(RiskScore)
		? FMath::Clamp(RiskScore + Violation.Score, 0.0f, 100000.0f)
		: 100000.0f;
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (URPGSecurityTelemetrySubsystem* Telemetry =
				GameInstance->GetSubsystem<
					URPGSecurityTelemetrySubsystem>())
			{
				Telemetry->EnqueueViolation(GetOwner(), Violation, RiskScore);
			}
		}
	}
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
	EvaluateEnforcementState();
}

bool URPGSecurityValidationComponent::CanPerformProtectedAction(
	FName ActionName,
	const bool bReportDeniedAttempt,
	FString& OutReason)
{
	OutReason.Reset();
	if (!IsAuthorityOwner())
	{
		OutReason = TEXT("Only authority may evaluate protected actions.");
		return false;
	}

	const FRPGSecurityEnforcementConfig& Enforcement =
		GetPolicyConfigRef().Enforcement;
	if (!Enforcement.bEnabled
		|| !Enforcement.bBlockProtectedActionsWhenRestricted
		|| EnforcementState < ERPGSecurityEnforcementState::Restricted)
	{
		return true;
	}

	if (ActionName.IsNone())
	{
		ActionName = TEXT("ProtectedAction");
	}
	OutReason = FString::Printf(
		TEXT("Action %s is blocked by server security state %s."),
		*ActionName.ToString(),
		*StaticEnum<ERPGSecurityEnforcementState>()->GetNameStringByValue(
			static_cast<int64>(EnforcementState)));
	if (bReportDeniedAttempt)
	{
		const double Now = GetServerTimeSeconds();
		const double* LastReport = LastDeniedActionReportByName.Find(ActionName);
		if (!LastReport || Now - *LastReport >= FMath::Max(
			0.1,
			static_cast<double>(
				Enforcement.DeniedActionReportIntervalSeconds)))
		{
			LastDeniedActionReportByName.Add(ActionName, Now);
			ReportViolation(
				ERPGSecurityViolationType::RestrictedActionAttempt,
				ERPGSecurityViolationSeverity::High,
				FMath::IsFinite(Enforcement.DeniedActionRiskScore)
					? FMath::Max(0.0f, Enforcement.DeniedActionRiskScore)
					: 0.0f,
				OutReason);
		}
	}
	return false;
}

bool URPGSecurityValidationComponent::RequestPlayerRemoval(
	const FText& Reason)
{
	if (!IsAuthorityOwner())
	{
		return false;
	}

	APlayerController* PlayerController = ResolveOwningPlayerController();
	UWorld* World = GetWorld();
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (!PlayerController || !GameMode || !GameMode->GameSession)
	{
		UE_LOG(
			LogRPGSecurity,
			Error,
			TEXT("Unable to remove %s because its server session is unavailable."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	const FText EffectiveReason = Reason.IsEmpty()
		? NSLOCTEXT(
			"RPGSecurity",
			"DefaultRemovalReason",
			"The server ended this session for security review.")
		: Reason;
	EmitEnforcementAuditEvent(
		ERPGSecurityViolationType::PlayerRemoval,
		ERPGSecurityViolationSeverity::Critical,
		FString::Printf(
			TEXT("Player removal requested at risk %.2f."),
			RiskScore));
	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		if (URPGSecurityTelemetrySubsystem* Telemetry =
			GameInstance->GetSubsystem<URPGSecurityTelemetrySubsystem>())
		{
			Telemetry->FlushNow();
		}
	}
	bRemovalScheduled = false;
	World->GetTimerManager().ClearTimer(AutomaticRemovalTimerHandle);
	const bool bRemoved = GameMode->GameSession->KickPlayer(
		PlayerController,
		EffectiveReason);
	if (bRemoved)
	{
		UE_LOG(
			LogRPGSecurity,
			Warning,
			TEXT("Security player removal Owner=%s Risk=%.2f Result=Removed"),
			*GetNameSafe(GetOwner()),
			RiskScore);
	}
	else
	{
		UE_LOG(
			LogRPGSecurity,
			Error,
			TEXT("Security player removal Owner=%s Risk=%.2f Result=Failed"),
			*GetNameSafe(GetOwner()),
			RiskScore);
	}
	return bRemoved;
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
	EvaluateEnforcementState();
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
	const float AuthoredDecay =
		GetPolicyConfigRef().Scoring.RiskDecayPerSecond;
	const float SafeDecay = FMath::IsFinite(AuthoredDecay)
		? FMath::Max(0.0f, AuthoredDecay)
		: 0.0f;
	RiskScore = FMath::Max(
		0.0f,
		RiskScore - SafeDecay *
			FMath::Max(0.0f, DeltaTime));
	if (bRiskThresholdBroadcast &&
		RiskScore < FMath::Max(
			1.0f,
			GetPolicyConfigRef().Scoring.RiskThreshold) * 0.5f)
	{
		bRiskThresholdBroadcast = false;
	}
	EvaluateEnforcementState();
}

void URPGSecurityValidationComponent::EvaluateEnforcementState()
{
	if (!IsAuthorityOwner())
	{
		return;
	}
	SetEnforcementState(FRPGSecurityEnforcementMath::ResolveState(
		RiskScore,
		EnforcementState,
		GetPolicyConfigRef().Enforcement));
}

void URPGSecurityValidationComponent::SetEnforcementState(
	const ERPGSecurityEnforcementState NewState)
{
	if (NewState == EnforcementState)
	{
		return;
	}

	const ERPGSecurityEnforcementState PreviousState = EnforcementState;
	EnforcementState = NewState;
	if (NewState != ERPGSecurityEnforcementState::RemovalRecommended)
	{
		bRemovalScheduled = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AutomaticRemovalTimerHandle);
		}
	}

	const UEnum* StateEnum = StaticEnum<ERPGSecurityEnforcementState>();
	const FString PreviousName = StateEnum
		? StateEnum->GetNameStringByValue(static_cast<int64>(PreviousState))
		: TEXT("Unknown");
	const FString NewName = StateEnum
		? StateEnum->GetNameStringByValue(static_cast<int64>(NewState))
		: TEXT("Unknown");
	ERPGSecurityViolationSeverity Severity =
		ERPGSecurityViolationSeverity::Low;
	switch (NewState)
	{
	case ERPGSecurityEnforcementState::RemovalRecommended:
		Severity = ERPGSecurityViolationSeverity::Critical;
		break;
	case ERPGSecurityEnforcementState::Restricted:
		Severity = ERPGSecurityViolationSeverity::High;
		break;
	case ERPGSecurityEnforcementState::Elevated:
		Severity = ERPGSecurityViolationSeverity::Medium;
		break;
	case ERPGSecurityEnforcementState::Monitoring:
	default:
		break;
	}
	const FString Detail = FString::Printf(
		TEXT("Enforcement state changed from %s to %s at risk %.2f."),
		*PreviousName,
		*NewName,
		RiskScore);
	EmitEnforcementAuditEvent(
		ERPGSecurityViolationType::EnforcementStateChanged,
		Severity,
		Detail);
	UE_LOG(
		LogRPGSecurity,
		Warning,
		TEXT("SecurityEnforcement Owner=%s Previous=%s New=%s Risk=%.2f"),
		*GetNameSafe(GetOwner()),
		*PreviousName,
		*NewName,
		RiskScore);
	OnEnforcementStateChanged.Broadcast(PreviousState, NewState, RiskScore);

	if (NewState == ERPGSecurityEnforcementState::RemovalRecommended)
	{
		ScheduleAutomaticRemoval();
	}
}

void URPGSecurityValidationComponent::EmitEnforcementAuditEvent(
	const ERPGSecurityViolationType Type,
	const ERPGSecurityViolationSeverity Severity,
	const FString& Detail)
{
	FRPGSecurityViolation AuditEvent;
	AuditEvent.Type = Type;
	AuditEvent.Severity = Severity;
	AuditEvent.Score = 0.0f;
	AuditEvent.ServerTimeSeconds = GetServerTimeSeconds();
	AuditEvent.Detail = RPGSecurity::SanitizeLogDetail(Detail);
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (URPGSecurityTelemetrySubsystem* Telemetry =
				GameInstance->GetSubsystem<
					URPGSecurityTelemetrySubsystem>())
			{
				Telemetry->EnqueueViolation(GetOwner(), AuditEvent, RiskScore);
			}
		}
	}
}

void URPGSecurityValidationComponent::ScheduleAutomaticRemoval()
{
	const FRPGSecurityEnforcementConfig& Enforcement =
		GetPolicyConfigRef().Enforcement;
	if (bRemovalScheduled || !Enforcement.bAutomaticallyRemovePlayer)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float RemovalDelay =
		FMath::IsFinite(Enforcement.AutomaticRemovalDelaySeconds)
			? FMath::Clamp(
				Enforcement.AutomaticRemovalDelaySeconds,
				0.1f,
				30.0f)
			: 2.0f;
	bRemovalScheduled = true;
	World->GetTimerManager().SetTimer(
		AutomaticRemovalTimerHandle,
		this,
		&URPGSecurityValidationComponent::ExecuteScheduledRemoval,
		RemovalDelay,
		false);
}

void URPGSecurityValidationComponent::ExecuteScheduledRemoval()
{
	bRemovalScheduled = false;
	if (EnforcementState !=
			ERPGSecurityEnforcementState::RemovalRecommended
		|| !GetPolicyConfigRef().Enforcement.bAutomaticallyRemovePlayer)
	{
		return;
	}
	RequestPlayerRemoval(NSLOCTEXT(
		"RPGSecurity",
		"AutomaticRemovalReason",
		"The server ended this session after repeated invalid gameplay requests."));
}

APlayerController*
URPGSecurityValidationComponent::ResolveOwningPlayerController() const
{
	if (APlayerController* DirectController =
		Cast<APlayerController>(GetOwner()))
	{
		return DirectController;
	}
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Cast<APlayerController>(Pawn->GetController());
	}
	if (const APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		return PlayerState->GetPlayerController();
	}
	return nullptr;
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
