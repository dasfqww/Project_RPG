#include "Skill/RPGSkillDefinition.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "DataTable/SkillData.h"
#include "Skill/RPGSkillAction.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"

#if WITH_EDITOR
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillDefinition)

const FPrimaryAssetType URPGSkillDefinition::PrimaryAssetType(
	TEXT("RPGSkillDefinition"));

URPGSkillDefinition::URPGSkillDefinition(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultTargetingPolicyClass =
		URPGSkillTargetingPolicy_CameraDirection::StaticClass();

	FRPGSkillCameraDirectionTargetingConfig CameraTargetingConfig;
	CameraTargetingConfig.bFlattenAimDirection = true;
	DefaultTargetingConfig.InitializeAs<
		FRPGSkillCameraDirectionTargetingConfig>(CameraTargetingConfig);
}

FPrimaryAssetId URPGSkillDefinition::GetPrimaryAssetId() const
{
	const FPrimaryAssetId StableId = MakePrimaryAssetIdForTag(SkillTag);
	return StableId.IsValid()
		? StableId
		: FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId URPGSkillDefinition::MakePrimaryAssetIdForTag(
	const FGameplayTag& InSkillTag)
{
	return InSkillTag.IsValid()
		? FPrimaryAssetId(PrimaryAssetType, InSkillTag.GetTagName())
		: FPrimaryAssetId();
}

#if WITH_EDITOR
EDataValidationResult URPGSkillDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result != EDataValidationResult::Invalid)
	{
		Result = EDataValidationResult::Valid;
	}
	const auto Fail = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!SkillTag.IsValid())
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "InvalidSkillTag",
			"Skill Tag must be valid."));
	}
	if (SkillName.IsEmpty())
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "EmptySkillName",
			"Skill Name cannot be empty."));
	}
	if (!SkillMontage)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "MissingSkillMontage",
			"Skill Montage must be assigned."));
	}
	if (!FMath::IsFinite(BaseCooldown) || BaseCooldown < 1.0f)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "InvalidBaseCooldown",
			"Base Cooldown must be finite and at least one second."));
	}
	if (MaxSkillLevel < 1)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "InvalidMaxSkillLevel",
			"Max Skill Level must be at least one."));
	}

	FRPGSkillRuntimeSpec RuntimeSpec;
	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);

	if (!DefaultExecutionPolicyClass)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "MissingExecutionPolicy",
			"Default Execution Policy Class must be assigned."));
	}
	else if (DefaultExecutionPolicyClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "AbstractExecutionPolicy",
			"Default Execution Policy Class cannot be abstract."));
	}
	else
	{
		const URPGSkillExecutionPolicy* Policy =
			DefaultExecutionPolicyClass->GetDefaultObject<
				URPGSkillExecutionPolicy>();
		FText ValidationError;
		const bool bConfigValid = Policy &&
			Policy->ValidateExecutionConfig(
				RuntimeSpec.ExecutionConfig, ValidationError);
		const bool bRuntimeSpecValid = bConfigValid &&
			Policy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError);
		if (!bRuntimeSpecValid)
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidExecutionData",
					"Default execution data is invalid: {0}"),
				ValidationError.IsEmpty()
					? NSLOCTEXT("RPGSkillDefinition", "UnavailableExecutionPolicy",
						"policy is unavailable")
					: ValidationError));
		}
	}

	if (!DefaultTargetingPolicyClass)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "MissingTargetingPolicy",
			"Default Targeting Policy Class must be assigned."));
	}
	else if (DefaultTargetingPolicyClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "AbstractTargetingPolicy",
			"Default Targeting Policy Class cannot be abstract."));
	}
	else
	{
		const URPGSkillTargetingPolicy* Policy =
			DefaultTargetingPolicyClass->GetDefaultObject<
				URPGSkillTargetingPolicy>();
		FText ValidationError;
		if (!Policy ||
			!Policy->ValidateTargetingConfig(
				DefaultTargetingConfig, ValidationError))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidTargetingData",
					"Default targeting data is invalid: {0}"),
				ValidationError.IsEmpty()
					? NSLOCTEXT("RPGSkillDefinition", "UnavailableTargetingPolicy",
						"policy is unavailable")
					: ValidationError));
		}
	}

	const auto ValidateExecutionOverride =
		[&RuntimeSpec, &Fail](
			const TSubclassOf<URPGSkillExecutionPolicy> PolicyOverride,
			const FInstancedStruct& ConfigOverride,
			const UAnimMontage* MontageOverride,
			const FText& Label)
	{
		FRPGSkillRuntimeSpec Candidate = RuntimeSpec;
		if (PolicyOverride)
		{
			Candidate.ExecutionPolicyClass = PolicyOverride;
			Candidate.ExecutionConfig = ConfigOverride;
		}
		else if (ConfigOverride.IsValid())
		{
			Candidate.ExecutionConfig = ConfigOverride;
		}
		if (MontageOverride)
		{
			Candidate.Montage = const_cast<UAnimMontage*>(MontageOverride);
		}

		const UClass* PolicyClass = Candidate.ExecutionPolicyClass.Get();
		if (!PolicyClass || PolicyClass->HasAnyClassFlags(CLASS_Abstract))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidOverrideExecutionClass",
					"{0} resolves to a missing or abstract execution policy."),
				Label));
			return;
		}

		const URPGSkillExecutionPolicy* Policy =
			PolicyClass->GetDefaultObject<URPGSkillExecutionPolicy>();
		FText ValidationError;
		if (!Policy ||
			!Policy->ValidateExecutionConfig(
				Candidate.ExecutionConfig, ValidationError) ||
			!Policy->ValidateRuntimeSpecData(Candidate, ValidationError))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidExecutionOverride",
					"{0} execution override is invalid: {1}"),
				Label,
				ValidationError.IsEmpty()
					? NSLOCTEXT("RPGSkillDefinition", "UnavailableOverrideExecution",
						"policy is unavailable")
					: ValidationError));
		}
	};

	const auto ValidateTargetingOverride =
		[&RuntimeSpec, &Fail](
			const TSubclassOf<URPGSkillTargetingPolicy> PolicyOverride,
			const FInstancedStruct& ConfigOverride,
			const FText& Label)
	{
		FRPGSkillRuntimeSpec Candidate = RuntimeSpec;
		if (PolicyOverride)
		{
			Candidate.TargetingPolicyClass = PolicyOverride;
			Candidate.TargetingConfig = ConfigOverride;
		}
		else if (ConfigOverride.IsValid())
		{
			Candidate.TargetingConfig = ConfigOverride;
		}

		const UClass* PolicyClass = Candidate.TargetingPolicyClass.Get();
		if (!PolicyClass || PolicyClass->HasAnyClassFlags(CLASS_Abstract))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidOverrideTargetingClass",
					"{0} resolves to a missing or abstract targeting policy."),
				Label));
			return;
		}

		const URPGSkillTargetingPolicy* Policy =
			PolicyClass->GetDefaultObject<URPGSkillTargetingPolicy>();
		FText ValidationError;
		if (!Policy ||
			!Policy->ValidateTargetingConfig(
				Candidate.TargetingConfig, ValidationError))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidTargetingOverride",
					"{0} targeting override is invalid: {1}"),
				Label,
				ValidationError.IsEmpty()
					? NSLOCTEXT("RPGSkillDefinition", "UnavailableOverrideTargeting",
						"policy is unavailable")
					: ValidationError));
		}
	};

	if (!FRPGHitQueryMath::IsShapeValid(TargetingProfile.Shape))
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "InvalidHitQueryShape",
			"Targeting Profile contains an invalid hit-query shape."));
	}
	if (TargetingProfile.Filter.MaxResults < 0)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "InvalidHitQueryLimit",
			"Targeting Profile Max Results cannot be negative."));
	}

	FString SecurityReason;
	if (!SecurityProfile.IsValid(&SecurityReason))
	{
		Fail(FText::Format(
			NSLOCTEXT("RPGSkillDefinition", "InvalidSecurityProfile",
				"Security Profile is invalid: {0}"),
			FText::FromString(SecurityReason)));
	}
	else if (TargetingProfile.Filter.MaxResults > 0 &&
		TargetingProfile.Filter.MaxResults >
			SecurityProfile.MaximumTargetsPerQuery)
	{
		Fail(NSLOCTEXT("RPGSkillDefinition", "QueryExceedsSecurityLimit",
			"Targeting Profile Max Results cannot exceed the security "
			"Maximum Targets Per Query."));
	}

	for (int32 TierIndex = 0; TierIndex < TripodTiers.Num(); ++TierIndex)
	{
		const FRPGSkillTripodTier& Tier = TripodTiers[TierIndex];
		if (Tier.RequiredSkillLevel < 1 ||
			Tier.RequiredSkillLevel > MaxSkillLevel)
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidTripodLevel",
					"Tripod tier {0} has a required level outside the "
					"skill's level range."),
				FText::AsNumber(TierIndex + 1)));
		}

		for (int32 OptionIndex = 0;
			OptionIndex < Tier.Options.Num(); ++OptionIndex)
		{
			const FRPGSkillTripodOption& Option = Tier.Options[OptionIndex];
			const FText OptionLabel = FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "TripodOptionLabel",
					"Tripod tier {0}, option {1}"),
				FText::AsNumber(TierIndex + 1),
				FText::AsNumber(OptionIndex + 1));
			if (Option.OverrideExecutionPolicyClass ||
				Option.OverrideExecutionConfig.IsValid() ||
				Option.OverrideMontage)
			{
				ValidateExecutionOverride(
					Option.OverrideExecutionPolicyClass,
					Option.OverrideExecutionConfig,
					Option.OverrideMontage,
					OptionLabel);
			}
			if (Option.OverrideTargetingPolicyClass ||
				Option.OverrideTargetingConfig.IsValid())
			{
				ValidateTargetingOverride(
					Option.OverrideTargetingPolicyClass,
					Option.OverrideTargetingConfig,
					OptionLabel);
			}
			for (const FRPGSkillModifier& Modifier : Option.StatModifiers)
			{
				if (!Modifier.StatTag.IsValid() ||
					!FMath::IsFinite(Modifier.ScalarValue))
				{
					Fail(FText::Format(
						NSLOCTEXT("RPGSkillDefinition", "InvalidTripodModifier",
							"Tripod tier {0}, option {1} contains an invalid "
							"stat modifier."),
						FText::AsNumber(TierIndex + 1),
						FText::AsNumber(OptionIndex + 1)));
				}
			}
		}
	}

	TSet<FGameplayTag> ModeStateTags;
	for (int32 OverrideIndex = 0;
		OverrideIndex < ModeOverrides.Num(); ++OverrideIndex)
	{
		const FSkillModeOverride& Override = ModeOverrides[OverrideIndex];
		const FText OverrideLabel = FText::Format(
			NSLOCTEXT("RPGSkillDefinition", "ModeOverrideLabel",
				"Mode override {0}"),
			FText::AsNumber(OverrideIndex + 1));
		if (!Override.RequiredStateTag.IsValid())
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidModeStateTag",
					"{0} requires a valid state tag."),
				OverrideLabel));
		}
		else if (ModeStateTags.Contains(Override.RequiredStateTag))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "DuplicateModeStateTag",
					"{0} duplicates an earlier required state tag."),
				OverrideLabel));
		}
		ModeStateTags.Add(Override.RequiredStateTag);

		if (!FMath::IsFinite(Override.DamageMultiplier) ||
			Override.DamageMultiplier < 0.0f)
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGSkillDefinition", "InvalidModeDamageMultiplier",
					"{0} has an invalid damage multiplier."),
				OverrideLabel));
		}
		if (Override.NewExecutionPolicyClass ||
			Override.NewExecutionConfig.IsValid() ||
			Override.NewMontage)
		{
			ValidateExecutionOverride(
				Override.NewExecutionPolicyClass,
				Override.NewExecutionConfig,
				Override.NewMontage,
				OverrideLabel);
		}
		if (Override.NewTargetingPolicyClass ||
			Override.NewTargetingConfig.IsValid())
		{
			ValidateTargetingOverride(
				Override.NewTargetingPolicyClass,
				Override.NewTargetingConfig,
				OverrideLabel);
		}
	}

	return Result == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid
		: EDataValidationResult::Valid;
}
#endif

int32 URPGSkillDefinition::ClampSkillLevel(
	const int32 RequestedLevel) const
{
	return FMath::Clamp(RequestedLevel, 1, FMath::Max(1, MaxSkillLevel));
}

bool URPGSkillDefinition::IsTripodSelectionAllowed(
	const int32 SkillLevel,
	const int32 TierIndex,
	const int32 OptionIndex) const
{
	if (!TripodTiers.IsValidIndex(TierIndex) || OptionIndex < INDEX_NONE)
	{
		return false;
	}
	if (OptionIndex == INDEX_NONE)
	{
		return true;
	}

	const FRPGSkillTripodTier& Tier = TripodTiers[TierIndex];
	return ClampSkillLevel(SkillLevel) >= Tier.RequiredSkillLevel &&
		Tier.Options.IsValidIndex(OptionIndex);
}

void URPGSkillDefinition::NormalizeSaveData(
	FRPGSkillSaveData& InOutSaveData) const
{
	InOutSaveData.SkillLevel = ClampSkillLevel(InOutSaveData.SkillLevel);
	while (InOutSaveData.SelectedTripodIndices.Num() < TripodTiers.Num())
	{
		InOutSaveData.SelectedTripodIndices.Add(INDEX_NONE);
	}
	if (InOutSaveData.SelectedTripodIndices.Num() > TripodTiers.Num())
	{
		InOutSaveData.SelectedTripodIndices.SetNum(TripodTiers.Num());
	}

	for (int32 TierIndex = 0; TierIndex < TripodTiers.Num(); ++TierIndex)
	{
		int32& Selection = InOutSaveData.SelectedTripodIndices[TierIndex];
		if (!IsTripodSelectionAllowed(
			InOutSaveData.SkillLevel,
			TierIndex,
			Selection))
		{
			Selection = INDEX_NONE;
		}
	}
}

void URPGSkillDefinition::BuildRuntimeSpec(
	AActor* InActor,
	const FRPGSkillSaveData& SaveData,
	FRPGSkillRuntimeSpec& OutSpec) const
{
	FRPGSkillSaveData NormalizedSaveData = SaveData;
	NormalizeSaveData(NormalizedSaveData);

	OutSpec.Reset();
	OutSpec.SkillTag = SkillTag;
	OutSpec.SkillLevel = NormalizedSaveData.SkillLevel;
	OutSpec.Icon = SkillIcon;
	OutSpec.Montage = SkillMontage;
	OutSpec.VFX = SkillVFX;
	OutSpec.ActionClass = DefaultActionClass;
	OutSpec.ExecutionPolicyClass = DefaultExecutionPolicyClass;
	OutSpec.ExecutionConfig = DefaultExecutionConfig;
	OutSpec.TargetingPolicyClass = DefaultTargetingPolicyClass;
	OutSpec.TargetingConfig = DefaultTargetingConfig;
	OutSpec.BaseCooldown = BaseCooldown;
	OutSpec.TargetingProfile = TargetingProfile;
	OutSpec.SecurityProfile = SecurityProfile;
	ApplyDefinitionExecutionDefaults(OutSpec);

	if (!SkillDataHandle.IsNull())
	{
		if (const FRPGSkillDataTable* Row =
			SkillDataHandle.GetRow<FRPGSkillDataTable>(TEXT("BuildRuntimeSpec")))
		{
			if (Row->SkillIcon)
			{
				OutSpec.Icon = Row->SkillIcon;
			}
		}
	}

	const int32 SelectionCount = FMath::Max(3, TripodTiers.Num());
	OutSpec.SelectedTripodIndices.Init(INDEX_NONE, SelectionCount);
	for (int32 TierIndex = 0; TierIndex < TripodTiers.Num(); ++TierIndex)
	{
		if (!NormalizedSaveData.SelectedTripodIndices.IsValidIndex(TierIndex))
		{
			continue;
		}

		const FRPGSkillTripodTier& Tier = TripodTiers[TierIndex];
		const int32 OptionIndex =
			NormalizedSaveData.SelectedTripodIndices[TierIndex];
		if (!Tier.Options.IsValidIndex(OptionIndex))
		{
			continue;
		}

		OutSpec.SelectedTripodIndices[TierIndex] = OptionIndex;
		const FRPGSkillTripodOption& Option = Tier.Options[OptionIndex];
		if (Option.TripodTag.IsValid())
		{
			OutSpec.TripodTags.AddTag(Option.TripodTag);
		}

		for (const FRPGSkillModifier& Modifier : Option.StatModifiers)
		{
			if (!Modifier.StatTag.IsValid() ||
				!FMath::IsFinite(Modifier.ScalarValue))
			{
				continue;
			}

			float& ComposedScalar = OutSpec.StatScalars.FindOrAdd(Modifier.StatTag, 1.0f);
			const float CandidateScalar =
				ComposedScalar * Modifier.ScalarValue;
			if (FMath::IsFinite(CandidateScalar))
			{
				ComposedScalar = CandidateScalar;
			}
		}

		if (Option.OverrideActionClass)
		{
			OutSpec.ActionClass = Option.OverrideActionClass;
		}
		if (Option.OverrideExecutionPolicyClass)
		{
			OutSpec.ExecutionPolicyClass = Option.OverrideExecutionPolicyClass;
			OutSpec.ExecutionConfig = Option.OverrideExecutionConfig;
		}
		else if (Option.OverrideExecutionConfig.IsValid())
		{
			OutSpec.ExecutionConfig = Option.OverrideExecutionConfig;
		}
		if (Option.OverrideTargetingPolicyClass)
		{
			OutSpec.TargetingPolicyClass = Option.OverrideTargetingPolicyClass;
			OutSpec.TargetingConfig = Option.OverrideTargetingConfig;
		}
		else if (Option.OverrideTargetingConfig.IsValid())
		{
			OutSpec.TargetingConfig = Option.OverrideTargetingConfig;
		}
		if (Option.OverrideMontage)
		{
			OutSpec.Montage = Option.OverrideMontage;
		}
		if (Option.OverrideVFX)
		{
			OutSpec.VFX = Option.OverrideVFX;
		}
	}

	UAbilitySystemComponent* ASC = InActor
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InActor)
		: nullptr;
	if (!ASC)
	{
		return;
	}

	// State overrides are intentionally resolved after tripods. This provides a
	// deterministic final layer for identities, stances, and transformation modes.
	for (const FSkillModeOverride& Override : ModeOverrides)
	{
		if (!Override.RequiredStateTag.IsValid() ||
			!ASC->HasMatchingGameplayTag(Override.RequiredStateTag))
		{
			continue;
		}

		if (Override.NewIcon)
		{
			OutSpec.Icon = Override.NewIcon;
		}
		if (Override.NewMontage)
		{
			OutSpec.Montage = Override.NewMontage;
		}
		if (Override.NewActionClass)
		{
			OutSpec.ActionClass = Override.NewActionClass;
		}
		if (Override.NewExecutionPolicyClass)
		{
			OutSpec.ExecutionPolicyClass = Override.NewExecutionPolicyClass;
			OutSpec.ExecutionConfig = Override.NewExecutionConfig;
		}
		else if (Override.NewExecutionConfig.IsValid())
		{
			OutSpec.ExecutionConfig = Override.NewExecutionConfig;
		}
		if (Override.NewTargetingPolicyClass)
		{
			OutSpec.TargetingPolicyClass = Override.NewTargetingPolicyClass;
			OutSpec.TargetingConfig = Override.NewTargetingConfig;
		}
		else if (Override.NewTargetingConfig.IsValid())
		{
			OutSpec.TargetingConfig = Override.NewTargetingConfig;
		}
		if (FMath::IsFinite(Override.DamageMultiplier) &&
			!FMath::IsNearlyEqual(Override.DamageMultiplier, 1.0f))
		{
			static const FGameplayTag DamageStatTag =
				FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"), false);
			if (DamageStatTag.IsValid())
			{
				float& DamageScalar = OutSpec.StatScalars.FindOrAdd(DamageStatTag, 1.0f);
				const float CandidateDamageScalar =
					DamageScalar * Override.DamageMultiplier;
				if (FMath::IsFinite(CandidateDamageScalar))
				{
					DamageScalar = CandidateDamageScalar;
				}
			}
		}
		break;
	}
}

void URPGSkillDefinition::GetSkillDataForContext(
	AActor* InActor,
	const TArray<int32>& SelectedTripods,
	UTexture2D*& OutIcon,
	UAnimMontage*& OutMontage,
	TSubclassOf<URPGSkillAction>& OutActionClass) const
{
	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = FMath::Max(1, MaxSkillLevel);
	SaveData.SelectedTripodIndices = SelectedTripods;

	FRPGSkillRuntimeSpec RuntimeSpec;
	BuildRuntimeSpec(InActor, SaveData, RuntimeSpec);
	OutIcon = RuntimeSpec.Icon;
	OutMontage = RuntimeSpec.Montage;
	OutActionClass = RuntimeSpec.ActionClass;
}

void URPGSkillDefinition::ApplyDefinitionExecutionDefaults(
	FRPGSkillRuntimeSpec& OutSpec) const
{
}
