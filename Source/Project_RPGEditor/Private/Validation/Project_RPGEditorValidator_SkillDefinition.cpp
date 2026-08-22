#include "Validation/Project_RPGEditorValidator_SkillDefinition.h"

#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillAction.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditorValidator_SkillDefinition"

bool UProject_RPGEditorValidator_SkillDefinition::CanValidateAsset_Implementation(
	UObject* InAsset) const
{
	return Super::CanValidateAsset_Implementation(InAsset) &&
		InAsset->IsA<URPGSkillDefinition>();
}

EDataValidationResult
UProject_RPGEditorValidator_SkillDefinition::ValidateLoadedAsset_Implementation(
	UObject* InAsset,
	TArray<FText>& ValidationErrors)
{
	URPGSkillDefinition* Definition = CastChecked<URPGSkillDefinition>(InAsset);
	EDataValidationResult Result = EDataValidationResult::Valid;
	const auto Fail = [this, Definition, &ValidationErrors, &Result](const FText& Message)
	{
		AssetFails(Definition, Message, ValidationErrors);
		Result = EDataValidationResult::Invalid;
	};
	const auto ValidateExecutionConfig =
		[&Fail](
			const TSubclassOf<URPGSkillExecutionPolicy> PolicyClass,
			const FInstancedStruct& Config,
			const FText& Context)
	{
		if (!PolicyClass)
		{
			return;
		}

		const URPGSkillExecutionPolicy* Policy =
			PolicyClass->GetDefaultObject<URPGSkillExecutionPolicy>();
		FText ConfigError;
		if (!Policy ||
			!Policy->ValidateExecutionConfig(Config, ConfigError))
		{
			Fail(FText::Format(
				LOCTEXT(
					"ExecutionConfigInvalid",
					"{0} has an invalid execution config: {1}"),
				Context,
				ConfigError));
		}
	};
	const auto ValidateTargetingConfig =
		[&Fail](
			const TSubclassOf<URPGSkillTargetingPolicy> PolicyClass,
			const FInstancedStruct& Config,
			const FText& Context)
	{
		if (!PolicyClass)
		{
			if (Config.IsValid())
			{
				Fail(FText::Format(
					LOCTEXT(
						"TargetingPolicyMissing",
						"{0} has targeting config but no targeting policy."),
					Context));
			}
			return;
		}

		const URPGSkillTargetingPolicy* Policy =
			PolicyClass->GetDefaultObject<URPGSkillTargetingPolicy>();
		FText ConfigError;
		if (!Policy ||
			!Policy->ValidateTargetingConfig(Config, ConfigError))
		{
			Fail(FText::Format(
				LOCTEXT(
					"TargetingConfigInvalid",
					"{0} has an invalid targeting config: {1}"),
				Context,
				ConfigError));
		}
	};

	if (Definition->SkillName.IsEmpty())
	{
		Fail(LOCTEXT("SkillNameEmpty", "Skill Name is empty."));
	}
	if (!Definition->SkillTag.IsValid())
	{
		Fail(LOCTEXT("SkillTagInvalid", "Skill Tag is invalid."));
	}
	FRPGSkillRuntimeSpec DefaultRuntimeSpec;
	Definition->BuildRuntimeSpec(
		nullptr,
		FRPGSkillSaveData(),
		DefaultRuntimeSpec);
	if (!DefaultRuntimeSpec.ExecutionPolicyClass &&
		!DefaultRuntimeSpec.ActionClass)
	{
		Fail(LOCTEXT(
			"DefaultExecutionMissing",
			"Assign an Execution Policy or a legacy Action Class."));
	}
	else if (DefaultRuntimeSpec.ExecutionPolicyClass)
	{
		ValidateExecutionConfig(
			DefaultRuntimeSpec.ExecutionPolicyClass,
			DefaultRuntimeSpec.ExecutionConfig,
			LOCTEXT("DefaultExecutionContext", "Default execution"));
	}
	ValidateTargetingConfig(
		DefaultRuntimeSpec.TargetingPolicyClass,
		DefaultRuntimeSpec.TargetingConfig,
		LOCTEXT("DefaultTargetingContext", "Default targeting"));
	FString SecurityProfileError;
	if (!DefaultRuntimeSpec.SecurityProfile.IsValid(&SecurityProfileError))
	{
		Fail(FText::Format(
			LOCTEXT(
				"SecurityProfileInvalid",
				"Security Profile is invalid: {0}"),
			FText::FromString(SecurityProfileError)));
	}
	if (Definition->MaxSkillLevel < 1)
	{
		Fail(LOCTEXT("MaxLevelInvalid", "Max Skill Level must be at least 1."));
	}
	if (!FMath::IsFinite(Definition->BaseCooldown) || Definition->BaseCooldown < 0.0f)
	{
		Fail(LOCTEXT("CooldownInvalid", "Base Cooldown must be finite and non-negative."));
	}
	if (!FRPGHitQueryMath::IsShapeValid(Definition->TargetingProfile.Shape))
	{
		Fail(LOCTEXT("TargetingShapeInvalid",
			"Targeting Profile contains invalid shape dimensions."));
	}
	if (Definition->TargetingProfile.Filter.ObjectTypes.IsEmpty())
	{
		Fail(LOCTEXT("TargetingObjectTypesEmpty",
			"Targeting Profile must contain at least one object type."));
	}
	if (Definition->TargetingProfile.Filter.MaxResults < 0)
	{
		Fail(LOCTEXT("TargetingMaxResultsInvalid",
			"Targeting Profile Max Results must be zero or greater."));
	}
	if (!Definition->SkillIcon && Definition->SkillDataHandle.IsNull())
	{
		Fail(LOCTEXT("IconMissing", "Assign a Skill Icon or a Skill Data row."));
	}
	if (Definition->TripodTiers.Num() > 3)
	{
		Fail(LOCTEXT("TooManyTiers", "The current tripod UI supports at most three tiers."));
	}
	for (const FSkillModeOverride& Override : Definition->ModeOverrides)
	{
		if (!Override.RequiredStateTag.IsValid())
		{
			Fail(LOCTEXT("ModeTagInvalid", "A Mode Override has an invalid required state tag."));
		}
		if (!FMath::IsFinite(Override.DamageMultiplier) || Override.DamageMultiplier < 0.0f)
		{
			Fail(LOCTEXT("ModeDamageInvalid",
				"A Mode Override has an invalid damage multiplier."));
		}
		if (Override.NewExecutionPolicyClass ||
			Override.NewExecutionConfig.IsValid())
		{
			ValidateExecutionConfig(
				Override.NewExecutionPolicyClass
					? Override.NewExecutionPolicyClass
					: DefaultRuntimeSpec.ExecutionPolicyClass,
				Override.NewExecutionPolicyClass
					? Override.NewExecutionConfig
					: (Override.NewExecutionConfig.IsValid()
						? Override.NewExecutionConfig
						: DefaultRuntimeSpec.ExecutionConfig),
				LOCTEXT("ModeExecutionContext", "A Mode Override"));
		}
		if (Override.NewTargetingPolicyClass ||
			Override.NewTargetingConfig.IsValid())
		{
			ValidateTargetingConfig(
				Override.NewTargetingPolicyClass
					? Override.NewTargetingPolicyClass
					: DefaultRuntimeSpec.TargetingPolicyClass,
				Override.NewTargetingPolicyClass
					? Override.NewTargetingConfig
					: (Override.NewTargetingConfig.IsValid()
						? Override.NewTargetingConfig
						: DefaultRuntimeSpec.TargetingConfig),
				LOCTEXT("ModeTargetingContext", "A Mode Override"));
		}
	}

	for (int32 TierIndex = 0; TierIndex < Definition->TripodTiers.Num(); ++TierIndex)
	{
		const FRPGSkillTripodTier& Tier = Definition->TripodTiers[TierIndex];
		const int32 MaxOptions = TierIndex < 2 ? 3 : 2;
		if (Tier.RequiredSkillLevel < 1 ||
			Tier.RequiredSkillLevel > Definition->MaxSkillLevel)
		{
			Fail(FText::Format(
				LOCTEXT("TierLevelInvalid", "Tripod tier {0} has an invalid required level."),
				FText::AsNumber(TierIndex + 1)));
		}
		if (Tier.Options.IsEmpty() || Tier.Options.Num() > MaxOptions)
		{
			Fail(FText::Format(
				LOCTEXT("TierOptionCountInvalid",
					"Tripod tier {0} must contain between 1 and {1} options."),
				FText::AsNumber(TierIndex + 1), FText::AsNumber(MaxOptions)));
		}

		for (int32 OptionIndex = 0; OptionIndex < Tier.Options.Num(); ++OptionIndex)
		{
			const FRPGSkillTripodOption& Option = Tier.Options[OptionIndex];
			if (Option.OptionName.IsEmpty())
			{
				Fail(FText::Format(
					LOCTEXT("OptionNameEmpty", "Tripod tier {0}, option {1} has no name."),
					FText::AsNumber(TierIndex + 1), FText::AsNumber(OptionIndex + 1)));
			}

			const bool bHasBehavior = !Option.StatModifiers.IsEmpty() ||
				Option.TripodTag.IsValid() || Option.OverrideActionClass ||
				Option.OverrideExecutionPolicyClass ||
				Option.OverrideExecutionConfig.IsValid() ||
				Option.OverrideTargetingPolicyClass ||
				Option.OverrideTargetingConfig.IsValid() ||
				Option.OverrideMontage || Option.OverrideVFX;
			if (!bHasBehavior)
			{
				Fail(FText::Format(
					LOCTEXT("OptionBehaviorEmpty",
						"Tripod tier {0}, option {1} does not change runtime behavior or presentation."),
					FText::AsNumber(TierIndex + 1), FText::AsNumber(OptionIndex + 1)));
			}
			if (Option.OverrideExecutionPolicyClass ||
				Option.OverrideExecutionConfig.IsValid())
			{
				ValidateExecutionConfig(
					Option.OverrideExecutionPolicyClass
						? Option.OverrideExecutionPolicyClass
						: DefaultRuntimeSpec.ExecutionPolicyClass,
					Option.OverrideExecutionPolicyClass
						? Option.OverrideExecutionConfig
						: (Option.OverrideExecutionConfig.IsValid()
							? Option.OverrideExecutionConfig
							: DefaultRuntimeSpec.ExecutionConfig),
					FText::Format(
						LOCTEXT(
							"TripodExecutionContext",
							"Tripod tier {0}, option {1}"),
						FText::AsNumber(TierIndex + 1),
						FText::AsNumber(OptionIndex + 1)));
			}
			if (Option.OverrideTargetingPolicyClass ||
				Option.OverrideTargetingConfig.IsValid())
			{
				ValidateTargetingConfig(
					Option.OverrideTargetingPolicyClass
						? Option.OverrideTargetingPolicyClass
						: DefaultRuntimeSpec.TargetingPolicyClass,
					Option.OverrideTargetingPolicyClass
						? Option.OverrideTargetingConfig
						: (Option.OverrideTargetingConfig.IsValid()
							? Option.OverrideTargetingConfig
							: DefaultRuntimeSpec.TargetingConfig),
					FText::Format(
						LOCTEXT(
							"TripodTargetingContext",
							"Tripod tier {0}, option {1}"),
						FText::AsNumber(TierIndex + 1),
						FText::AsNumber(OptionIndex + 1)));
			}

			TSet<FGameplayTag> ModifierTags;
			for (const FRPGSkillModifier& Modifier : Option.StatModifiers)
			{
				if (!Modifier.StatTag.IsValid() || !FMath::IsFinite(Modifier.ScalarValue) ||
					Modifier.ScalarValue < 0.0f)
				{
					Fail(FText::Format(
						LOCTEXT("ModifierInvalid",
							"Tripod tier {0}, option {1} contains an invalid stat modifier."),
						FText::AsNumber(TierIndex + 1), FText::AsNumber(OptionIndex + 1)));
					continue;
				}
				if (ModifierTags.Contains(Modifier.StatTag))
				{
					Fail(FText::Format(
						LOCTEXT("ModifierDuplicate",
							"Tripod tier {0}, option {1} repeats the same Stat Tag."),
						FText::AsNumber(TierIndex + 1), FText::AsNumber(OptionIndex + 1)));
				}
				ModifierTags.Add(Modifier.StatTag);
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
