#include "Skill/RPGSkillRuntimeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillRuntimeTypes)

void FRPGSkillRuntimeSpec::Reset()
{
	SkillTag = FGameplayTag::EmptyTag;
	SkillLevel = 1;
	SelectedTripodIndices.Reset();
	TripodTags.Reset();
	StatScalars.Reset();
	Icon = nullptr;
	Montage = nullptr;
	VFX = nullptr;
	ActionClass = nullptr;
	ExecutionPolicyClass = nullptr;
	ExecutionConfig.Reset();
	TargetingPolicyClass = nullptr;
	TargetingConfig.Reset();
	BaseCooldown = 1.0f;
	TargetingProfile = FRPGHitQueryProfile();
	SecurityProfile = FRPGSkillSecurityProfile();
}

float FRPGSkillRuntimeSpec::GetStatScalar(
	const FGameplayTag& StatTag,
	const float DefaultValue) const
{
	if (const float* Value = StatScalars.Find(StatTag))
	{
		return *Value;
	}
	return DefaultValue;
}

bool FRPGSkillRuntimeSpec::HasTripodTag(const FGameplayTag& TripodTag) const
{
	return TripodTag.IsValid() && TripodTags.HasTagExact(TripodTag);
}

float FRPGSkillRuntimeSpec::GetCooldownDuration(
	const float MinimumDuration) const
{
	const float SafeMinimum = FMath::IsFinite(MinimumDuration)
		? FMath::Max(MinimumDuration, 1.0f)
		: 1.0f;
	const float SafeBaseCooldown = FMath::IsFinite(BaseCooldown)
		? FMath::Max(BaseCooldown, 0.0f)
		: SafeMinimum;

	const FGameplayTag CooldownStatTag =
		FGameplayTag::RequestGameplayTag(
			TEXT("Shared.Stat.Cooldown"),
			false);
	const float CooldownScalar = CooldownStatTag.IsValid()
		? GetStatScalar(CooldownStatTag)
		: 1.0f;
	const float ScaledDuration = FMath::IsFinite(CooldownScalar)
		? SafeBaseCooldown * FMath::Max(CooldownScalar, 0.0f)
		: SafeMinimum;
	return FMath::Max(ScaledDuration, SafeMinimum);
}
