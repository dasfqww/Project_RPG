#include "Skill/RPGSkillDefinition.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "DataTable/SkillData.h"
#include "Skill/RPGSkillAction.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillDefinition)

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

void URPGSkillDefinition::BuildRuntimeSpec(
	AActor* InActor,
	const FRPGSkillSaveData& SaveData,
	FRPGSkillRuntimeSpec& OutSpec) const
{
	OutSpec.Reset();
	OutSpec.SkillTag = SkillTag;
	OutSpec.SkillLevel = FMath::Clamp(SaveData.SkillLevel, 1, FMath::Max(1, MaxSkillLevel));
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
		if (!SaveData.SelectedTripodIndices.IsValidIndex(TierIndex))
		{
			continue;
		}

		const FRPGSkillTripodTier& Tier = TripodTiers[TierIndex];
		const int32 OptionIndex = SaveData.SelectedTripodIndices[TierIndex];
		if (OutSpec.SkillLevel < Tier.RequiredSkillLevel ||
			!Tier.Options.IsValidIndex(OptionIndex))
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
