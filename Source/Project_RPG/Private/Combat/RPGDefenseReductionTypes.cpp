#include "Combat/RPGDefenseReductionTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Status, "Shared.Status.Debuff.DefenseReduced");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Immunity, "Shared.Immunity.Status.DefenseReduction");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Percent, "Ability.Data.Status.DefenseReductionPercent");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Duration, "Ability.Data.Status.DefenseReductionSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Multiplier, "Effect.Data.Status.DefenseReductionMultiplier");
}
bool FRPGDefenseReductionSettings::IsValid() const
{
	return FMath::IsFinite(Percent) && (Percent == 0.f || (Percent >= 1.f && Percent <= 80.f))
		&& FMath::IsFinite(Duration) && Duration >= .05f && Duration <= 30.f;
}
FGameplayTag RPGDefenseReduction::StatusTag() { return TAG_Status; }
FGameplayTag RPGDefenseReduction::ImmunityTag() { return TAG_Immunity; }
FGameplayTag RPGDefenseReduction::MagnitudeTag() { return TAG_Multiplier; }
bool RPGDefenseReduction::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGDefenseReductionSettings& Settings)
{
	if (!Settings.IsValid()) return false;
	Spec.SetByCallerTagMagnitudes.Remove(TAG_Percent);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_Duration);
	if (Settings.IsEnabled())
	{
		Spec.SetByCallerTagMagnitudes.Add(TAG_Percent, Settings.Percent);
		Spec.SetByCallerTagMagnitudes.Add(TAG_Duration, Settings.Duration);
	}
	return true;
}
FRPGDefenseReductionSettings RPGDefenseReduction::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGDefenseReductionSettings Result;
	const auto* Percent = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Percent) : nullptr;
	const auto* Duration = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Duration) : nullptr;
	if (!Percent || !Duration) return Result;
	Result.Percent = *Percent; Result.Duration = *Duration;
	return Result.IsValid() ? Result : FRPGDefenseReductionSettings();
}
