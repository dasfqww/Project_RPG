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
	OutError = FText::GetEmpty();
	return true;
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

bool URPGSkillExecutionPolicy_Instant::ValidateRuntimeSpec(
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpec(OutError))
	{
		return false;
	}
	const FRPGSkillInstantExecutionConfig* Config =
		GetRuntimeSpec().ExecutionConfig
		.GetPtr<FRPGSkillInstantExecutionConfig>();
	if (!GetRuntimeSpec().Montage ||
		(Config && !HasMontageSection(
			GetRuntimeSpec().Montage,
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

bool URPGSkillExecutionPolicy_Charge::ValidateRuntimeSpec(
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpec(OutError))
	{
		return false;
	}
	const FRPGSkillChargeExecutionConfig* Config = GetChargeConfig();
	if (!Config || !GetRuntimeSpec().Montage ||
		!HasMontageSection(GetRuntimeSpec().Montage, Config->ChargeSection) ||
		!HasMontageSection(GetRuntimeSpec().Montage, Config->ReleaseSection))
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

bool URPGSkillExecutionPolicy_Holding::ValidateRuntimeSpec(
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpec(OutError))
	{
		return false;
	}
	const FRPGSkillHoldingExecutionConfig* Config = GetHoldingConfig();
	if (!Config || !GetRuntimeSpec().Montage ||
		!HasMontageSection(GetRuntimeSpec().Montage, Config->HoldingSection) ||
		!HasMontageSection(GetRuntimeSpec().Montage, Config->SuccessSection) ||
		!HasMontageSection(GetRuntimeSpec().Montage, Config->FailureSection))
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

bool URPGSkillExecutionPolicy_Combo::ValidateRuntimeSpec(
	FText& OutError) const
{
	if (!Super::ValidateRuntimeSpec(OutError))
	{
		return false;
	}
	const FRPGSkillComboExecutionConfig* Config = GetComboConfig();
	if (!Config || !GetRuntimeSpec().Montage)
	{
		return FailRuntimeValidation(
			OutError,
			TEXT("Combo policy requires a montage."));
	}
	for (const FName Section : Config->ComboSections)
	{
		if (!HasMontageSection(GetRuntimeSpec().Montage, Section))
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
