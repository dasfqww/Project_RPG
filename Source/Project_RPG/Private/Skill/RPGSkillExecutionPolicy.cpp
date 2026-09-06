#include "Skill/RPGSkillExecutionPolicy.h"

#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimMontage.h"
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

bool URPGSkillExecutionPolicy::StartExecution()
{
	return Host != nullptr;
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
	return ValidateRuntimeSpecData(GetRuntimeSpec(), OutError);
}

bool URPGSkillExecutionPolicy::ValidateRuntimeSpecData(
	const FRPGSkillRuntimeSpec& RuntimeSpec,
	FText& OutError) const
{
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
}

void URPGSkillExecutionPolicy::CancelExecution()
{
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

bool URPGSkillExecutionPolicy_Charge::StartExecution()
{
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

	CurrentChargeLevel = 0;
	bReachedMaximumCharge = false;
	bReleased = false;
	ChargeStartTime = World->GetTimeSeconds();

	GetHost()->ShowSkillProgress();
	GetHost()->StartSkillPersistentVFX();
	if (!GetHost()->PlaySkillMontage(Config->ChargeSection))
	{
		GetHost()->StopSkillPersistentVFX();
		GetHost()->HideSkillProgress();
		return false;
	}

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
		GetHost()->HideSkillProgress();
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
		GetHost()->HideSkillProgress();
	}
	Super::CancelExecution();
}

void URPGSkillExecutionPolicy_Charge::UpdateCharge()
{
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	UWorld* World = GetWorld();
	if (!Config || !World || bReleased)
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
	GetHost()->UpdateSkillProgress(
		FMath::Min(Elapsed, MaximumChargeTime),
		MaximumChargeTime);

	if (CurrentChargeLevel >= Config->MaxChargeLevel)
	{
		if (!bReachedMaximumCharge)
		{
			bReachedMaximumCharge = true;
			GetHost()->NotifySkillProgressCompleted();
		}

		if (Elapsed >= MaximumChargeTime + Config->MaxChargeHoldTime)
		{
			ReleaseCharge();
		}
	}
}

void URPGSkillExecutionPolicy_Charge::ReleaseCharge()
{
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	if (!Config || !GetHost())
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ChargeUpdateTimerHandle);
	}
	GetHost()->StopSkillPersistentVFX();
	GetHost()->HideSkillProgress();

	if (CurrentChargeLevel < Config->MinimumReleaseLevel)
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}

	if (!GetHost()->RefreshSkillTarget())
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}

	if (!GetHost()->JumpToSkillMontageSection(Config->ReleaseSection))
	{
		GetHost()->FinishSkillExecution(true);
	}
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

	GetHost()->ShowSkillProgress();
	GetHost()->StartSkillPersistentVFX();
	if (!GetHost()->PlaySkillMontage(Config->HoldingSection))
	{
		CleanupHolding();
		return false;
	}

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
			TEXT("Holding timing or success section is invalid."));
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
	OutError = FText::GetEmpty();
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
	CleanupHolding();
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Holding::CancelExecution()
{
	CleanupHolding();
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
	GetHost()->UpdateSkillProgress(
		FMath::Min(Elapsed, HoldDuration),
		HoldDuration);

	if (!bPerfectZoneReached && Elapsed >= PerfectZoneStart)
	{
		bPerfectZoneReached = true;
		GetHost()->NotifySkillProgressCompleted();
	}

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
	CleanupHolding();
	if (!Config)
	{
		GetHost()->FinishSkillExecution(true);
		return;
	}

	const FName Section =
		bSuccessful ? Config->SuccessSection : Config->FailureSection;
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

	if (!GetHost()->JumpToSkillMontageSection(Section))
	{
		GetHost()->FinishSkillExecution(true);
	}
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
		GetHost()->HideSkillProgress();
	}
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

	GetHost()->ShowSkillProgress();
	GetHost()->StartSkillPersistentVFX();
	if (!GetHost()->PlaySkillMontage(Config->CastingSection))
	{
		CleanupCasting();
		return false;
	}

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
	CleanupCasting();
	Super::EndExecution();
}

void URPGSkillExecutionPolicy_Casting::CancelExecution()
{
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
	GetHost()->UpdateSkillProgress(
		FMath::Min(Elapsed, CastDuration),
		CastDuration);
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
	GetHost()->NotifySkillProgressCompleted();
	CleanupCasting();
	if (!Config ||
		!GetHost()->JumpToSkillMontageSection(Config->CompleteSection))
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
	CleanupCasting();
	if (!Config || Config->CancelSection.IsNone() ||
		!GetHost()->JumpToSkillMontageSection(Config->CancelSection))
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
		GetHost()->HideSkillProgress();
	}
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
