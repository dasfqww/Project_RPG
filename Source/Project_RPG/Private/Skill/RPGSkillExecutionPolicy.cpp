#include "Skill/RPGSkillExecutionPolicy.h"
#include "Combat/RPGCombatControlTypes.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimMontage.h"
#include "AnimNotify/RPGAnimNotify_SkillFinisher.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillExecutionPolicy)

namespace
{
bool HasMontageSection(
	const UAnimMontage* Montage,
	const FName SectionName)
{
	return SectionName.IsNone() ||
		(Montage && Montage->GetSectionIndex(SectionName) != INDEX_NONE);
}

bool FailRuntimeValidation(FText& OutError, const TCHAR* Message)
{
	OutError = FText::FromString(Message);
	return false;
}
}

void URPGSkillExecutionPolicy::Initialize(IRPGSkillExecutionHost& InHost)
{
	Host = &InHost;
}

FText URPGSkillExecutionPolicy::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "CustomExecution", "Custom");
}

bool URPGSkillExecutionPolicy::StartExecution()
{
	bProgressExecutionEnded = false;
	EndProgress(ERPGActionProgressEndReason::Cancelled);
	return Host != nullptr;
}

void URPGSkillExecutionPolicy::BeginProgress(ERPGActionProgressKind Kind, float Duration, int32 StageCount)
{
	EndProgress(ERPGActionProgressEndReason::Cancelled);
	if (!Host || HasEndedExecution()) return;
	APawn* Pawn = Cast<APawn>(Host->GetSkillSourceActor());
	if (!IsValid(Pawn) || !Pawn->IsLocallyControlled() || Pawn->GetNetMode() == NM_DedicatedServer) return;
	auto* UI = Pawn->FindComponentByClass<UPlayerUIComponent>();
	if (!IsValid(UI)) return;
	ProgressData = {};
	ProgressData.Kind = Kind;
	ProgressData.Label = GetRuntimeSpec().DisplayName.IsEmpty() ? GetExecutionDisplayName() : GetRuntimeSpec().DisplayName;
	ProgressData.Icon = GetRuntimeSpec().Icon;
	ProgressData.bHasDuration = true;
	ProgressData.DurationSeconds = Duration;
	ProgressData.TimeDisplay = ERPGActionProgressTimeDisplay::Elapsed;
	ProgressData.StageCount = StageCount;
	ProgressUI = UI;
	const uint64 Revision = ProgressRevision;
	const auto Handle = UI->BeginActionProgress(this, ProgressData);
	// Begin can synchronously cancel this policy before its returned handle is stored.
	if (Revision != ProgressRevision || HasEndedExecution())
	{
		UI->EndActionProgress(Handle, ERPGActionProgressEndReason::Cancelled);
		return;
	}
	ProgressHandle = Handle;
}

void URPGSkillExecutionPolicy::UpdateProgress(float Elapsed, float Duration, int32 Stage)
{
	if (HasEndedExecution()) return;
	ProgressData.ElapsedSeconds = Elapsed;
	ProgressData.DurationSeconds = Duration;
	ProgressData.Stage = Stage;
	if (auto* UI = ProgressUI.Get()) UI->UpdateActionProgress(ProgressHandle, ProgressData);
}

void URPGSkillExecutionPolicy::ReadyProgress()
{
	if (HasEndedExecution()) return;
	ProgressData.Phase = ERPGActionProgressPhase::Ready;
	if (auto* UI = ProgressUI.Get()) UI->UpdateActionProgress(ProgressHandle, ProgressData);
}

void URPGSkillExecutionPolicy::EndProgress(ERPGActionProgressEndReason Reason)
{
	++ProgressRevision;
	auto* UI = ProgressUI.Get();
	const auto Handle = ProgressHandle;
	ProgressUI.Reset();
	ProgressHandle = {};
	ProgressData = {};
	if (IsValid(UI)) UI->EndActionProgress(Handle, Reason);
}

bool URPGSkillExecutionPolicy::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy::ValidateRuntimeSpec(FText& OutError) const
{
	if (!Host)
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Execution policy has no initialized host."));
	}
	if (!ValidateRuntimeSpecData(GetRuntimeSpec(), OutError)) return false;
	float MovementScale = 1.f;
	if (!GetRuntimeSpec().TryGetMontageMovementScale(MovementScale)) return false;
	if (MovementScale != 1.f)
	{
		const auto* Character = Cast<ACharacter>(Host->GetSkillSourceActor());
		if (!IsValid(Character) || Character->GetWorld() != GetWorld()
			|| (!Character->HasAuthority() && !Character->IsLocallyControlled()))
			return FailRuntimeValidation(OutError, TEXT("Scaled montage movement requires the casting character."));
		const auto* Security = Character->FindComponentByClass<URPGSecurityValidationComponent>();
		if (Character->HasAuthority() && (!Security || Security->IsMovementAuthorizationActive()))
			return FailRuntimeValidation(OutError, TEXT("Scaled montage movement has no free server authorization window."));
	}
	return true;
}

bool URPGSkillExecutionPolicy::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	FRPGCombatContributionAmounts ContributionAmounts;
	if (!RuntimeSpec.TryGetCombatContributions(ContributionAmounts))
		return FailRuntimeValidation(OutError, TEXT("Invalid frozen combat contribution amounts."));
	if (!RuntimeSpec.ContributionHitSection.IsNone())
	{
		const UAnimMontage* Montage = RuntimeSpec.Montage;
		const int32 Section = Montage ? Montage->GetSectionIndex(RuntimeSpec.ContributionHitSection) : INDEX_NONE;
		if (!SupportsContributionFinisher() || Section == INDEX_NONE)
			return FailRuntimeValidation(OutError, TEXT("Finisher contributions require a supported policy and an authored montage section."));
		int32 NotifyCount = 0;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.Notify && Event.Notify->IsA<URPGAnimNotify_SkillFinisher>()
				&& Montage->GetSectionIndexFromPosition(Event.GetTime()) == Section) ++NotifyCount;
		}
		if (NotifyCount != 1)
			return FailRuntimeValidation(OutError, TEXT("The contribution section requires exactly one native finishing-strike notify."));
	}
	float MovementScale = 1.f;
	if (!RuntimeSpec.TryGetMontageMovementScale(MovementScale))
		return FailRuntimeValidation(OutError, TEXT("Invalid montage movement scalar or bounded authorization profile."));
	if (!RPGHitReaction::IsValidPolicy(RuntimeSpec.HitReactionPolicy))
	{
		OutError = NSLOCTEXT("RPGSkillExecution", "InvalidHitReaction", "Invalid attack hit reaction policy.");
		return false;
	}
	if (!RPGCombatControl::IsValidStunDuration(RuntimeSpec.StunDuration))
	{
		OutError = NSLOCTEXT("RPGSkillExecution", "InvalidStunDuration", "Invalid attack stun duration.");
		return false;
	}
	if (!RuntimeSpec.Knockback.IsValid())
	{
		OutError = NSLOCTEXT("RPGSkillExecution", "InvalidKnockback", "Invalid attack knockback settings.");
		return false;
	}
	if (!RuntimeSpec.Slow.IsValid())
	{
		OutError = NSLOCTEXT("RPGSkillExecution", "InvalidSlow", "Invalid attack slow settings.");
		return false;
	}
	if (!RuntimeSpec.ActivationImmunities.IsValid())
	{
		OutError = NSLOCTEXT("RPGSkillExecution", "InvalidImmunities", "Invalid activation immunity settings.");
		return false;
	}
	return ValidateExecutionConfig(RuntimeSpec.ExecutionConfig, OutError);
}

void URPGSkillExecutionPolicy::OnInputPressed()
{
}

void URPGSkillExecutionPolicy::OnInputReleased()
{
}

FGameplayTag URPGSkillExecutionPolicy::GetExecutionEventTag() const
{
	return FGameplayTag();
}

void URPGSkillExecutionPolicy::OnExecutionEvent(
	const FGameplayEventData& Payload)
{
}

void URPGSkillExecutionPolicy::OnMontageCompleted()
{
	if (Host)
	{
		Host->FinishSkillExecution(false);
	}
}

void URPGSkillExecutionPolicy::OnMontageInterrupted()
{
	if (Host)
	{
		Host->FinishSkillExecution(true);
	}
}

void URPGSkillExecutionPolicy::EndExecution()
{
	bProgressExecutionEnded = true;
	EndProgress(ERPGActionProgressEndReason::Completed);
}

void URPGSkillExecutionPolicy::CancelExecution()
{
	bProgressExecutionEnded = true;
	EndProgress(ERPGActionProgressEndReason::Cancelled);
}

const FRPGSkillRuntimeSpec& URPGSkillExecutionPolicy::GetRuntimeSpec() const
{
	check(Host);
	return Host->GetSkillRuntimeSpec();
}

UWorld* URPGSkillExecutionPolicy::GetWorld() const
{
	return Host ? Host->GetSkillExecutionWorld() : nullptr;
}

FText URPGSkillExecutionPolicy_Instant::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Instant", "Instant");
}

bool URPGSkillExecutionPolicy_Instant::StartExecution()
{
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!GetRuntimeSpec().Montage)
	{
		return false;
	}

	const FRPGSkillInstantExecutionConfig* Config =
		GetRuntimeSpec().ExecutionConfig.GetPtr<FRPGSkillInstantExecutionConfig>();
	const FName StartSection = Config ? Config->StartSection : NAME_None;
	return GetHost()->PlaySkillMontage(StartSection);
}

bool URPGSkillExecutionPolicy_Instant::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	if (!Config.IsValid() ||
		Config.GetPtr<FRPGSkillInstantExecutionConfig>())
	{
		OutError = FText::GetEmpty();
		return true;
	}

	OutError = FText::FromString(
		TEXT("Instant policy requires an Instant execution config."));
	return false;
}

bool URPGSkillExecutionPolicy_Instant::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillInstantExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig
		.GetPtr<FRPGSkillInstantExecutionConfig>();
	if (!RuntimeSpec.Montage ||
		(Config && !HasMontageSection(
			RuntimeSpec.Montage,
			Config->StartSection)))
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Instant policy references a missing montage or section."));
	}
	OutError = FText::GetEmpty();
	return true;
}

FText URPGSkillExecutionPolicy_Charge::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Charge", "Charge");
}

bool URPGSkillExecutionPolicy_Charge::StartExecution()
{
	// A failed/reused attempt must not expose the previous release receipt.
	CurrentChargeLevel = 0;
	bReachedMaximumCharge = false;
	bReleased = false;
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	UWorld* World = GetWorld();
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!Config || !World || !GetRuntimeSpec().Montage)
	{
		return false;
	}

	ChargeStartTime = World->GetTimeSeconds();

	const FName ChargeSection = Config->ChargeSection;
	BeginProgress(ERPGActionProgressKind::Charge, GetChargeTimePerLevel() * Config->MaxChargeLevel, Config->MaxChargeLevel);
	if (HasEndedExecution()) return false;
	GetHost()->StartSkillPersistentVFX();
	if (HasEndedExecution()) return false;
	if (!GetHost()->PlaySkillMontage(ChargeSection))
	{
		GetHost()->StopSkillPersistentVFX();
		EndProgress(ERPGActionProgressEndReason::Interrupted);
		return false;
	}
	if (HasEndedExecution()) return false;

	World->GetTimerManager().SetTimer(
		ChargeUpdateTimerHandle,
		this,
		&ThisClass::UpdateCharge,
		1.0f / 30.0f,
		true);
	UpdateCharge();
	return true;
}

bool URPGSkillExecutionPolicy_Charge::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	const FRPGSkillChargeExecutionConfig* ChargeConfig =
		Config.GetPtr<FRPGSkillChargeExecutionConfig>();
	if (!ChargeConfig)
	{
		OutError = FText::FromString(
			TEXT("Charge policy requires a Charge execution config."));
		return false;
	}

	const bool bValid =
		FMath::IsFinite(ChargeConfig->ChargeTimePerLevel) &&
		ChargeConfig->ChargeTimePerLevel > 0.0f &&
		ChargeConfig->MaxChargeLevel >= 1 &&
		FMath::IsFinite(ChargeConfig->MaxChargeHoldTime) &&
		ChargeConfig->MaxChargeHoldTime >= 0.0f &&
		ChargeConfig->MinimumReleaseLevel >= 0 &&
		ChargeConfig->MinimumReleaseLevel <= ChargeConfig->MaxChargeLevel &&
		!ChargeConfig->ReleaseSection.IsNone();
	if (!bValid)
	{
		OutError = FText::FromString(
			TEXT("Charge timing, levels, or release section are invalid."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Charge::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillChargeExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
	if (!Config || !RuntimeSpec.Montage ||
		!HasMontageSection(RuntimeSpec.Montage, Config->ChargeSection) ||
		!HasMontageSection(RuntimeSpec.Montage, Config->ReleaseSection))
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Charge policy references a missing montage or section."));
	}
	OutError = FText::GetEmpty();
	return true;
}

void URPGSkillExecutionPolicy_Charge::OnInputReleased()
{
	if (!bReleased)
	{
		ReleaseCharge();
	}
}

void URPGSkillExecutionPolicy_Charge::EndExecution()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeUpdateTimerHandle);
	}
	if (GetHost())
	{
		GetHost()->StopSkillPersistentVFX();
	}
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Charge::CancelExecution()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeUpdateTimerHandle);
	}
	if (GetHost())
	{
		GetHost()->StopSkillPersistentVFX();
	}
	Super::CancelExecution();
}

void URPGSkillExecutionPolicy_Charge::UpdateCharge()
{
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	UWorld* World = GetWorld();
	if (!Config || !World || bReleased || HasEndedExecution())
	{
		return;
	}

	const float TimePerLevel = GetChargeTimePerLevel();
	const float MaximumChargeTime = TimePerLevel * Config->MaxChargeLevel;
	const float Elapsed = FMath::Max(0.0f, World->GetTimeSeconds() - ChargeStartTime);

	CurrentChargeLevel = FMath::Clamp(
		FMath::FloorToInt(Elapsed / TimePerLevel),
		0,
		Config->MaxChargeLevel);
	const int32 MaxLevel = Config->MaxChargeLevel;
	const float MaxHold = Config->MaxChargeHoldTime;
	UpdateProgress(
		FMath::Min(Elapsed, MaximumChargeTime),
		MaximumChargeTime, CurrentChargeLevel);
	if (HasEndedExecution()) return;

	if (CurrentChargeLevel >= MaxLevel)
	{
		if (!bReachedMaximumCharge)
		{
			bReachedMaximumCharge = true;
			ReadyProgress();
		}
		if (HasEndedExecution()) return;

		if (Elapsed >= MaximumChargeTime + MaxHold)
		{
			ReleaseCharge();
		}
	}
}

void URPGSkillExecutionPolicy_Charge::ReleaseCharge()
{
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	if (!Config || !GetHost() || HasEndedExecution())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float TimePerLevel = GetChargeTimePerLevel();
		const float Elapsed = FMath::Max(
			0.0f,
			World->GetTimeSeconds() - ChargeStartTime);
		CurrentChargeLevel = FMath::Clamp(
			FMath::FloorToInt(Elapsed / TimePerLevel),
			0,
			Config->MaxChargeLevel);
	}

	bReleased = true;
	// Capture at release, including a release between the final level and timer update.
	// The owning ability clears its RuntimeSpec before GAS end listeners run.
	bReachedMaximumCharge = CurrentChargeLevel >= Config->MaxChargeLevel;
	const int32 MinimumLevel = Config->MinimumReleaseLevel;
	const FName ReleaseSection = Config->ReleaseSection;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeUpdateTimerHandle);
	}
	GetHost()->StopSkillPersistentVFX();
	if (HasEndedExecution()) return;

	if (CurrentChargeLevel < MinimumLevel)
	{
		EndProgress(ERPGActionProgressEndReason::Cancelled);
		if (HasEndedExecution()) return;
		GetHost()->FinishSkillExecution(true);
		return;
	}

	if (!GetHost()->RefreshSkillTarget())
	{
		EndProgress(ERPGActionProgressEndReason::Cancelled);
		if (HasEndedExecution()) return;
		GetHost()->FinishSkillExecution(true);
		return;
	}

	if (HasEndedExecution()) return;
	const bool bJumped = GetHost()->JumpToSkillMontageSection(ReleaseSection);
	EndProgress(bJumped ? ERPGActionProgressEndReason::Completed : ERPGActionProgressEndReason::Interrupted);
	if (!HasEndedExecution() && !bJumped)
	{
		GetHost()->FinishSkillExecution(true);
	}
}

bool URPGSkillExecutionPolicy_Charge::WasReleasedAtMaximumCharge() const
{
	return bReleased && bReachedMaximumCharge;
}

float URPGSkillExecutionPolicy_Charge::GetChargeTimePerLevel() const
{
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	if (!Config)
	{
		return 0.01f;
	}

	static const FGameplayTag ChargeTimeStatTag =
		FGameplayTag::RequestGameplayTag(
			TEXT("Shared.Stat.ChargeTime"),
			false);
	const float AuthoredScalar =
		GetRuntimeSpec().GetStatScalar(ChargeTimeStatTag);
	const float SafeScalar = FMath::IsFinite(AuthoredScalar)
		? FMath::Max(AuthoredScalar, 0.01f)
		: 1.0f;
	return FMath::Max(
		0.01f,
		Config->ChargeTimePerLevel * SafeScalar);
}

const FRPGSkillChargeExecutionConfig*
URPGSkillExecutionPolicy_Charge::GetChargeConfig() const
{
	return GetRuntimeSpec().ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
}

FText URPGSkillExecutionPolicy_Holding::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Holding", "Holding");
}

bool URPGSkillExecutionPolicy_Holding::StartExecution()
{
	const FRPGSkillHoldingExecutionConfig* Config = GetHoldingConfig();
	UWorld* World = GetWorld();
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!Config || !World || !GetRuntimeSpec().Montage)
	{
		return false;
	}

	HoldingStartTime = World->GetTimeSeconds();
	bPerfectZoneReached = false;
	bResolved = false;
	bFinishAsCancelled = false;
	bExecutionEnded = false;
	bSuccessfulFinishEntered = false;
	if (!ValidateRuntimeSpec(ValidationError)) return false;
	if (Config->bConstrainMovement)
	{
		auto* Character = Cast<ACharacter>(GetHost()->GetSkillSourceActor());
		auto* Movement = Character ? Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
		if (!Movement) return false;
		MovementConstraintHandle = Movement->BeginSkillMovementConstraint(this, Config->MovementSpeedScalar);
		if (!MovementConstraintHandle.IsValid()) return false;
		ConstrainedMovement = Movement;
	}

	BeginProgress(ERPGActionProgressKind::Holding, GetScaledTime(Config->HoldDuration));
	if (bResolved) return false;
	GetHost()->StartSkillPersistentVFX();
	if (bResolved) return false;
	if (!GetHost()->PlaySkillMontage(Config->HoldingSection))
	{
		CleanupHolding();
		ReleaseMovementConstraint();
		return false;
	}
	if (bResolved) return false;

	World->GetTimerManager().SetTimer(
		HoldingUpdateTimerHandle,
		this,
		&ThisClass::UpdateHolding,
		1.0f / 30.0f,
		true);
	UpdateHolding();
	return true;
}

bool URPGSkillExecutionPolicy_Holding::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	const FRPGSkillHoldingExecutionConfig* HoldingConfig =
		Config.GetPtr<FRPGSkillHoldingExecutionConfig>();
	if (!HoldingConfig)
	{
		OutError = FText::FromString(
			TEXT("Holding policy requires a Holding execution config."));
		return false;
	}

	const float PerfectZoneEnd =
		HoldingConfig->PerfectZoneEndTime > 0.0f
			? HoldingConfig->PerfectZoneEndTime
			: HoldingConfig->HoldDuration;
	const bool bValid =
		FMath::IsFinite(HoldingConfig->MovementSpeedScalar) &&
		HoldingConfig->MovementSpeedScalar >= 0.f && HoldingConfig->MovementSpeedScalar <= 1.f &&
		FMath::IsFinite(HoldingConfig->HoldDuration) &&
		HoldingConfig->HoldDuration > 0.0f &&
		FMath::IsFinite(HoldingConfig->PerfectZoneStartTime) &&
		HoldingConfig->PerfectZoneStartTime >= 0.0f &&
		FMath::IsFinite(PerfectZoneEnd) &&
		PerfectZoneEnd >= HoldingConfig->PerfectZoneStartTime &&
		PerfectZoneEnd <= HoldingConfig->HoldDuration &&
		!HoldingConfig->SuccessSection.IsNone();
	if (!bValid)
	{
		OutError = FText::FromString(
			TEXT("Holding timing, movement scalar, or success section is invalid."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Holding::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillHoldingExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillHoldingExecutionConfig>();
	if (!Config || !RuntimeSpec.Montage ||
		!HasMontageSection(RuntimeSpec.Montage, Config->HoldingSection) ||
		!HasMontageSection(RuntimeSpec.Montage, Config->SuccessSection) ||
		!HasMontageSection(RuntimeSpec.Montage, Config->FailureSection))
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Holding policy references a missing montage or section."));
	}
	if (!RuntimeSpec.ContributionHitSection.IsNone()
		&& (RuntimeSpec.ContributionHitSection != Config->SuccessSection
			|| RuntimeSpec.ContributionHitSection == Config->HoldingSection))
		return FailRuntimeValidation(OutError, TEXT("Holding finisher contributions require a distinct success section."));
	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Holding::ValidateRuntimeSpec(FText& OutError) const
{
	if (!Super::ValidateRuntimeSpec(OutError)) return false;
	const auto* Config = GetHoldingConfig();
	if (Config && Config->bConstrainMovement)
	{
		const auto* Character = GetHost() ? Cast<ACharacter>(GetHost()->GetSkillSourceActor()) : nullptr;
		if (!IsValid(Character) || Character->GetWorld() != GetWorld()
			|| (!Character->HasAuthority() && !Character->IsLocallyControlled())
			|| !Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()))
			return FailRuntimeValidation(OutError, TEXT("Movement-constrained holding requires the casting RPG character."));
	}
	return true;
}

void URPGSkillExecutionPolicy_Holding::OnInputReleased()
{
	if (bResolved)
	{
		return;
	}

	const FRPGSkillHoldingExecutionConfig* Config = GetHoldingConfig();
	UWorld* World = GetWorld();
	if (!Config || !World)
	{
		CompleteHolding(false);
		return;
	}

	const float Elapsed =
		FMath::Max(0.0f, World->GetTimeSeconds() - HoldingStartTime);
	const float PerfectZoneStart =
		GetScaledTime(Config->PerfectZoneStartTime);
	const float PerfectZoneEnd = GetScaledTime(
		Config->PerfectZoneEndTime > 0.0f
			? Config->PerfectZoneEndTime
			: Config->HoldDuration);
	CompleteHolding(
		Elapsed >= PerfectZoneStart &&
		Elapsed <= PerfectZoneEnd + KINDA_SMALL_NUMBER);
}

void URPGSkillExecutionPolicy_Holding::OnMontageCompleted()
{
	if (GetHost())
	{
		GetHost()->FinishSkillExecution(
			!bResolved || bFinishAsCancelled);
	}
}

void URPGSkillExecutionPolicy_Holding::EndExecution()
{
	bSuccessfulFinishEntered = false;
	bExecutionEnded = true;
	bResolved = true;
	CleanupHolding();
	ReleaseMovementConstraint();
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Holding::CancelExecution()
{
	bSuccessfulFinishEntered = false;
	bExecutionEnded = true;
	bResolved = true;
	CleanupHolding();
	ReleaseMovementConstraint();
	Super::CancelExecution();
}

void URPGSkillExecutionPolicy_Holding::UpdateHolding()
{
	const FRPGSkillHoldingExecutionConfig* Config = GetHoldingConfig();
	UWorld* World = GetWorld();
	if (!Config || !World || bResolved)
	{
		return;
	}

	const float HoldDuration = GetScaledTime(Config->HoldDuration);
	const float PerfectZoneStart =
		GetScaledTime(Config->PerfectZoneStartTime);
	const float PerfectZoneEnd = GetScaledTime(
		Config->PerfectZoneEndTime > 0.0f
			? Config->PerfectZoneEndTime
			: Config->HoldDuration);
	const float Elapsed =
		FMath::Max(0.0f, World->GetTimeSeconds() - HoldingStartTime);
	UpdateProgress(
		FMath::Min(Elapsed, HoldDuration),
		HoldDuration);
	if (bResolved) return;

	if (!bPerfectZoneReached && Elapsed >= PerfectZoneStart)
	{
		bPerfectZoneReached = true;
		ReadyProgress();
	}
	if (bResolved) return;

	if (Config->bAutoReleaseAtPerfectZoneEnd &&
		Elapsed >= PerfectZoneEnd)
	{
		CompleteHolding(true);
	}
	else if (Elapsed >= HoldDuration)
	{
		CompleteHolding(false);
	}
}

void URPGSkillExecutionPolicy_Holding::CompleteHolding(
	const bool bSuccessful)
{
	if (bResolved || !GetHost())
	{
		return;
	}

	bResolved = true;
	bFinishAsCancelled = !bSuccessful;
	const FRPGSkillHoldingExecutionConfig* Config = GetHoldingConfig();
	const FName Section = Config
		? (bSuccessful ? Config->SuccessSection : Config->FailureSection) : NAME_None;
	CleanupHolding();
	if (bExecutionEnded) return;
	if (!Config)
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}
	if (Section.IsNone())
	{
		GetHost()->FinishSkillExecution(!bSuccessful);
		return;
	}

	if (bSuccessful && !GetHost()->RefreshSkillTarget())
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}
	if (bExecutionEnded) return;

	if (!GetHost()->JumpToSkillMontageSection(Section))
	{
		GetHost()->FinishSkillExecution(true);
	}
	else if (!bExecutionEnded)
	{
		bSuccessfulFinishEntered = bSuccessful;
	}
}

bool URPGSkillExecutionPolicy_Holding::IsContributionFinisherReady() const
{
	return bSuccessfulFinishEntered && bResolved && !bFinishAsCancelled && !bExecutionEnded;
}

void URPGSkillExecutionPolicy_Holding::CleanupHolding()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldingUpdateTimerHandle);
	}
	if (GetHost())
	{
		GetHost()->StopSkillPersistentVFX();
	}
	EndProgress(!bResolved ? ERPGActionProgressEndReason::Interrupted
		: bFinishAsCancelled ? ERPGActionProgressEndReason::Cancelled : ERPGActionProgressEndReason::Completed);
}

void URPGSkillExecutionPolicy_Holding::ReleaseMovementConstraint()
{
	if (auto* Movement = ConstrainedMovement.Get()) Movement->EndSkillMovementConstraint(MovementConstraintHandle);
	MovementConstraintHandle.Invalidate();
	ConstrainedMovement.Reset();
}

void URPGSkillExecutionPolicy_Holding::BeginDestroy()
{
	// No host/world access during GC; release only the component we acquired.
	ReleaseMovementConstraint();
	Super::BeginDestroy();
}

float URPGSkillExecutionPolicy_Holding::GetScaledTime(
	const float AuthoredTime) const
{
	if (AuthoredTime <= 0.0f)
	{
		return 0.0f;
	}
	static const FGameplayTag HoldTimeStatTag =
		FGameplayTag::RequestGameplayTag(
			TEXT("Shared.Stat.HoldTime"),
			false);
	const float AuthoredScalar =
		GetRuntimeSpec().GetStatScalar(HoldTimeStatTag);
	const float SafeScalar = FMath::IsFinite(AuthoredScalar)
		? FMath::Max(AuthoredScalar, 0.01f)
		: 1.0f;
	return FMath::Max(0.01f, AuthoredTime * SafeScalar);
}

const FRPGSkillHoldingExecutionConfig*
URPGSkillExecutionPolicy_Holding::GetHoldingConfig() const
{
	return GetRuntimeSpec().ExecutionConfig
		.GetPtr<FRPGSkillHoldingExecutionConfig>();
}

FText URPGSkillExecutionPolicy_Casting::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Casting", "Casting");
}

bool URPGSkillExecutionPolicy_Casting::StartExecution()
{
	const FRPGSkillCastingExecutionConfig* Config = GetCastingConfig();
	UWorld* World = GetWorld();
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!Config || !World || !GetRuntimeSpec().Montage)
	{
		return false;
	}

	CastingStartTime = World->GetTimeSeconds();
	bResolved = false;
	bFinishAsCancelled = false;

	const FName CastingSection = Config->CastingSection;
	BeginProgress(ERPGActionProgressKind::Casting, GetScaledCastDuration());
	if (HasEndedExecution()) return false;
	GetHost()->StartSkillPersistentVFX();
	if (HasEndedExecution()) return false;
	if (!GetHost()->PlaySkillMontage(CastingSection))
	{
		CleanupCasting();
		return false;
	}
	if (HasEndedExecution()) return false;

	World->GetTimerManager().SetTimer(
		CastingUpdateTimerHandle,
		this,
		&ThisClass::UpdateCasting,
		1.0f / 30.0f,
		true);
	UpdateCasting();
	return true;
}

bool URPGSkillExecutionPolicy_Casting::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	const FRPGSkillCastingExecutionConfig* CastingConfig =
		Config.GetPtr<FRPGSkillCastingExecutionConfig>();
	if (!CastingConfig)
	{
		OutError = FText::FromString(
			TEXT("Casting policy requires a Casting execution config."));
		return false;
	}

	const bool bValid =
		FMath::IsFinite(CastingConfig->CastDuration) &&
		CastingConfig->CastDuration > 0.0f &&
		CastingConfig->CastDuration <= 60.0f &&
		!CastingConfig->CompleteSection.IsNone();
	if (!bValid)
	{
		OutError = FText::FromString(
			TEXT("Casting duration or completion section is invalid."));
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Casting::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillCastingExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillCastingExecutionConfig>();
	if (!Config || !RuntimeSpec.Montage ||
		!HasMontageSection(RuntimeSpec.Montage, Config->CastingSection) ||
		!HasMontageSection(RuntimeSpec.Montage, Config->CompleteSection) ||
		!HasMontageSection(RuntimeSpec.Montage, Config->CancelSection))
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Casting policy references a missing montage or section."));
	}
	OutError = FText::GetEmpty();
	return true;
}

void URPGSkillExecutionPolicy_Casting::OnInputReleased()
{
	const FRPGSkillCastingExecutionConfig* Config = GetCastingConfig();
	if (!bResolved && Config && Config->bCancelOnInputRelease)
	{
		CancelCasting();
	}
}

void URPGSkillExecutionPolicy_Casting::OnMontageCompleted()
{
	CleanupCasting();
	if (GetHost())
	{
		GetHost()->FinishSkillExecution(
			!bResolved || bFinishAsCancelled);
	}
}

void URPGSkillExecutionPolicy_Casting::OnMontageInterrupted()
{
	CleanupCasting();
	Super::OnMontageInterrupted();
}

void URPGSkillExecutionPolicy_Casting::EndExecution()
{
	bResolved = true;
	CleanupCasting();
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Casting::CancelExecution()
{
	bResolved = true;
	bFinishAsCancelled = true;
	CleanupCasting();
	Super::CancelExecution();
}

void URPGSkillExecutionPolicy_Casting::UpdateCasting()
{
	UWorld* World = GetWorld();
	if (!World || bResolved)
	{
		return;
	}

	const float CastDuration = GetScaledCastDuration();
	const float Elapsed =
		FMath::Max(0.0f, World->GetTimeSeconds() - CastingStartTime);
	UpdateProgress(
		FMath::Min(Elapsed, CastDuration),
		CastDuration);
	if (HasEndedExecution() || bResolved) return;
	if (Elapsed >= CastDuration)
	{
		CompleteCasting();
	}
}

void URPGSkillExecutionPolicy_Casting::CompleteCasting()
{
	if (bResolved || !GetHost())
	{
		return;
	}

	bResolved = true;
	bFinishAsCancelled = false;
	const FRPGSkillCastingExecutionConfig* Config = GetCastingConfig();
	const FName Section = Config ? Config->CompleteSection : NAME_None;
	ReadyProgress();
	if (HasEndedExecution()) return;
	CleanupCasting();
	if (HasEndedExecution()) return;
	if (Section.IsNone() || !GetHost()->JumpToSkillMontageSection(Section))
	{
		GetHost()->FinishSkillExecution(true);
	}
}

void URPGSkillExecutionPolicy_Casting::CancelCasting()
{
	if (bResolved || !GetHost())
	{
		return;
	}

	bResolved = true;
	bFinishAsCancelled = true;
	const FRPGSkillCastingExecutionConfig* Config = GetCastingConfig();
	const FName Section = Config ? Config->CancelSection : NAME_None;
	CleanupCasting();
	if (HasEndedExecution()) return;
	if (Section.IsNone() || !GetHost()->JumpToSkillMontageSection(Section))
	{
		GetHost()->FinishSkillExecution(true);
	}
}

void URPGSkillExecutionPolicy_Casting::CleanupCasting()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CastingUpdateTimerHandle);
	}
	if (GetHost())
	{
		GetHost()->StopSkillPersistentVFX();
	}
	EndProgress(!bResolved ? ERPGActionProgressEndReason::Interrupted
		: bFinishAsCancelled ? ERPGActionProgressEndReason::Cancelled : ERPGActionProgressEndReason::Completed);
}

float URPGSkillExecutionPolicy_Casting::GetScaledCastDuration() const
{
	const FRPGSkillCastingExecutionConfig* Config = GetCastingConfig();
	if (!Config)
	{
		return 0.01f;
	}

	static const FGameplayTag CastTimeStatTag =
		FGameplayTag::RequestGameplayTag(
			TEXT("Shared.Stat.CastTime"),
			false);
	const float AuthoredScalar =
		GetRuntimeSpec().GetStatScalar(CastTimeStatTag);
	const float SafeScalar = FMath::IsFinite(AuthoredScalar)
		? FMath::Max(AuthoredScalar, 0.01f)
		: 1.0f;
	const float ScaledDuration = Config->CastDuration * SafeScalar;
	return FMath::Clamp(
		FMath::IsFinite(ScaledDuration) ? ScaledDuration : 60.0f,
		0.01f,
		60.0f);
}

const FRPGSkillCastingExecutionConfig*
URPGSkillExecutionPolicy_Casting::GetCastingConfig() const
{
	return GetRuntimeSpec().ExecutionConfig
		.GetPtr<FRPGSkillCastingExecutionConfig>();
}

FText URPGSkillExecutionPolicy_Combo::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Combo", "Combo");
}

bool URPGSkillExecutionPolicy_Combo::StartExecution()
{
	const FRPGSkillComboExecutionConfig* Config = GetComboConfig();
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!Config || !GetRuntimeSpec().Montage ||
		!GetExecutionEventTag().IsValid())
	{
		return false;
	}

	CurrentComboIndex = 0;
	bAdvanceBuffered = false;
	return GetHost()->PlaySkillMontage(Config->ComboSections[0]);
}

bool URPGSkillExecutionPolicy_Combo::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	const FRPGSkillComboExecutionConfig* ComboConfig =
		Config.GetPtr<FRPGSkillComboExecutionConfig>();
	if (!ComboConfig || ComboConfig->ComboSections.IsEmpty())
	{
		OutError = FText::FromString(
			TEXT("Combo policy requires a Combo config with at least one section."));
		return false;
	}

	for (const FName Section : ComboConfig->ComboSections)
	{
		if (Section.IsNone())
		{
			OutError = FText::FromString(
				TEXT("Combo sections cannot contain None."));
			return false;
		}
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Combo::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillComboExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillComboExecutionConfig>();
	if (!Config || !RuntimeSpec.Montage)
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Combo policy requires a montage."));
	}
	for (const FName Section : Config->ComboSections)
	{
		if (!HasMontageSection(RuntimeSpec.Montage, Section))
		{
			return FailRuntimeValidation(
				OutError,
				TEXT("Combo policy references a missing montage section."));
		}
	}
	OutError = FText::GetEmpty();
	return true;
}

void URPGSkillExecutionPolicy_Combo::OnInputPressed()
{
	const FRPGSkillComboExecutionConfig* Config = GetComboConfig();
	if (Config && Config->bAllowRepeatedPressBuffer)
	{
		bAdvanceBuffered = true;
	}
}

FGameplayTag URPGSkillExecutionPolicy_Combo::GetExecutionEventTag() const
{
	const FRPGSkillComboExecutionConfig* Config = GetComboConfig();
	if (Config && Config->AdvanceEventTag.IsValid())
	{
		return Config->AdvanceEventTag;
	}

	return FGameplayTag::RequestGameplayTag(
		TEXT("GameplayEvent.Skill.Combo.Advance"),
		false);
}

void URPGSkillExecutionPolicy_Combo::OnExecutionEvent(
	const FGameplayEventData& Payload)
{
	const FRPGSkillComboExecutionConfig* Config = GetComboConfig();
	if (!Config || Payload.EventTag != GetExecutionEventTag() ||
		!Config->ComboSections.IsValidIndex(CurrentComboIndex + 1))
	{
		return;
	}

	const bool bAdvanceFromHeldInput =
		Config->bAdvanceWhileInputHeld &&
		GetHost() &&
		GetHost()->IsSkillInputPressed();
	if (!bAdvanceFromHeldInput && !bAdvanceBuffered)
	{
		return;
	}

	bAdvanceBuffered = false;
	const int32 NextComboIndex = CurrentComboIndex + 1;
	if (!GetHost()->JumpToSkillMontageSection(
		Config->ComboSections[NextComboIndex]))
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}

	CurrentComboIndex = NextComboIndex;
}

const FRPGSkillComboExecutionConfig*
URPGSkillExecutionPolicy_Combo::GetComboConfig() const
{
	return GetRuntimeSpec().ExecutionConfig.GetPtr<FRPGSkillComboExecutionConfig>();
}

FText URPGSkillExecutionPolicy_Chain::GetExecutionDisplayName() const
{
	return NSLOCTEXT("RPGSkillUI", "Chain", "Chain");
}

bool URPGSkillExecutionPolicy_Chain::StartExecution()
{
	const FRPGSkillChainExecutionConfig* Config = GetChainConfig();
	FText ValidationError;
	if (!Super::StartExecution() ||
		!ValidateExecutionConfig(
			GetRuntimeSpec().ExecutionConfig,
			ValidationError) ||
		!Config || !GetRuntimeSpec().Montage ||
		!GetExecutionEventTag().IsValid())
	{
		return false;
	}

	CurrentChainIndex = 0;
	bLinkWindowOpen = false;
	bInputBuffered = false;
	return GetHost()->PlaySkillMontage(Config->ChainSections[0]);
}

bool URPGSkillExecutionPolicy_Chain::ValidateExecutionConfig(
	const FInstancedStruct& Config,
	FText& OutError) const
{
	const FRPGSkillChainExecutionConfig* ChainConfig =
		Config.GetPtr<FRPGSkillChainExecutionConfig>();
	if (!ChainConfig || ChainConfig->ChainSections.Num() < 2)
	{
		OutError = FText::FromString(
			TEXT("Chain policy requires at least two chain sections."));
		return false;
	}

	if (!FMath::IsFinite(ChainConfig->LinkWindowDuration) ||
		ChainConfig->LinkWindowDuration < 0.05f ||
		ChainConfig->LinkWindowDuration > 5.0f)
	{
		OutError = FText::FromString(
			TEXT("Chain link-window duration must be between 0.05 and 5 seconds."));
		return false;
	}

	for (const FName Section : ChainConfig->ChainSections)
	{
		if (Section.IsNone())
		{
			OutError = FText::FromString(
				TEXT("Chain sections cannot contain None."));
			return false;
		}
	}

	OutError = FText::GetEmpty();
	return true;
}

bool URPGSkillExecutionPolicy_Chain::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpecData(RuntimeSpec, OutError))
	{
		return false;
	}
	const FRPGSkillChainExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChainExecutionConfig>();
	const FGameplayTag EventTag = Config && Config->LinkWindowEventTag.IsValid()
		? Config->LinkWindowEventTag
		: FGameplayTag::RequestGameplayTag(
			TEXT("GameplayEvent.Skill.Chain.Window"),
			false);
	if (!Config || !RuntimeSpec.Montage || !EventTag.IsValid())
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Chain policy requires a montage and a valid window event tag."));
	}
	for (const FName Section : Config->ChainSections)
	{
		if (!HasMontageSection(RuntimeSpec.Montage, Section))
		{
			return FailRuntimeValidation(
				OutError,
				TEXT("Chain policy references a missing montage section."));
		}
	}
	OutError = FText::GetEmpty();
	return true;
}

void URPGSkillExecutionPolicy_Chain::OnInputPressed()
{
	const FRPGSkillChainExecutionConfig* Config = GetChainConfig();
	if (!Config ||
		!Config->ChainSections.IsValidIndex(CurrentChainIndex + 1))
	{
		return;
	}

	if (bLinkWindowOpen)
	{
		AdvanceChain();
	}
	else if (Config->bAllowEarlyPressBuffer)
	{
		bInputBuffered = true;
	}
}

FGameplayTag URPGSkillExecutionPolicy_Chain::GetExecutionEventTag() const
{
	const FRPGSkillChainExecutionConfig* Config = GetChainConfig();
	if (Config && Config->LinkWindowEventTag.IsValid())
	{
		return Config->LinkWindowEventTag;
	}

	return FGameplayTag::RequestGameplayTag(
		TEXT("GameplayEvent.Skill.Chain.Window"),
		false);
}

void URPGSkillExecutionPolicy_Chain::OnExecutionEvent(
	const FGameplayEventData& Payload)
{
	const FRPGSkillChainExecutionConfig* Config = GetChainConfig();
	UWorld* World = GetWorld();
	if (!Config || !World || Payload.EventTag != GetExecutionEventTag() ||
		!Config->ChainSections.IsValidIndex(CurrentChainIndex + 1))
	{
		return;
	}

	World->GetTimerManager().ClearTimer(LinkWindowTimerHandle);
	bLinkWindowOpen = true;
	const bool bAdvanceFromHeldInput =
		Config->bAdvanceWhileInputHeld &&
		GetHost() && GetHost()->IsSkillInputPressed();
	if (bAdvanceFromHeldInput || bInputBuffered)
	{
		AdvanceChain();
		return;
	}

	World->GetTimerManager().SetTimer(
		LinkWindowTimerHandle,
		this,
		&ThisClass::CloseLinkWindow,
		Config->LinkWindowDuration,
		false);
}

void URPGSkillExecutionPolicy_Chain::OnMontageCompleted()
{
	CleanupChain();
	Super::OnMontageCompleted();
}

void URPGSkillExecutionPolicy_Chain::EndExecution()
{
	CleanupChain();
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Chain::CancelExecution()
{
	CleanupChain();
	Super::CancelExecution();
}

void URPGSkillExecutionPolicy_Chain::AdvanceChain()
{
	const FRPGSkillChainExecutionConfig* Config = GetChainConfig();
	if (!Config || !GetHost() ||
		!Config->ChainSections.IsValidIndex(CurrentChainIndex + 1))
	{
		CloseLinkWindow();
		return;
	}

	const int32 NextChainIndex = CurrentChainIndex + 1;
	CloseLinkWindow();
	if (!GetHost()->JumpToSkillMontageSection(
		Config->ChainSections[NextChainIndex]))
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}
	CurrentChainIndex = NextChainIndex;
}

void URPGSkillExecutionPolicy_Chain::CloseLinkWindow()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LinkWindowTimerHandle);
	}
	bLinkWindowOpen = false;
	bInputBuffered = false;
}

void URPGSkillExecutionPolicy_Chain::CleanupChain()
{
	CloseLinkWindow();
	CurrentChainIndex = INDEX_NONE;
}

const FRPGSkillChainExecutionConfig*
URPGSkillExecutionPolicy_Chain::GetChainConfig() const
{
	return GetRuntimeSpec().ExecutionConfig
		.GetPtr<FRPGSkillChainExecutionConfig>();
}
