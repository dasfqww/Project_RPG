#include "Combat/RPGSlowTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Slow, "Shared.Status.Movement.Slowed");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SlowImmunity, "Shared.Immunity.Movement.Slow");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SlowPercent, "Ability.Data.Status.SlowPercent");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SlowDuration, "Ability.Data.Status.SlowSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_SlowMultiplier, "Effect.Data.Status.SlowMultiplier");
}
bool FRPGSlowSettings::IsValid() const
{
	return FMath::IsFinite(Percent) && (Percent == 0.f || (Percent >= 5.f && Percent <= 90.f))
		&& FMath::IsFinite(Duration) && Duration >= .05f && Duration <= 30.f;
}
FGameplayTag RPGSlow::StatusTag() { return TAG_Slow; }
FGameplayTag RPGSlow::ImmunityTag() { return TAG_SlowImmunity; }
FGameplayTag RPGSlow::MagnitudeTag() { return TAG_SlowMultiplier; }
bool RPGSlow::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGSlowSettings& Settings)
{
	if (!Settings.IsValid()) return false;
	Spec.SetByCallerTagMagnitudes.Remove(TAG_SlowPercent); Spec.SetByCallerTagMagnitudes.Remove(TAG_SlowDuration);
	if (Settings.IsEnabled())
	{
		Spec.SetByCallerTagMagnitudes.Add(TAG_SlowPercent, Settings.Percent);
		Spec.SetByCallerTagMagnitudes.Add(TAG_SlowDuration, Settings.Duration);
	}
	return true;
}
FRPGSlowSettings RPGSlow::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGSlowSettings Result;
	const auto* Percent = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_SlowPercent) : nullptr;
	const auto* Duration = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_SlowDuration) : nullptr;
	if (!Percent || !Duration) return Result;
	Result.Percent = *Percent; Result.Duration = *Duration;
	return Result.IsValid() ? Result : FRPGSlowSettings();
}
