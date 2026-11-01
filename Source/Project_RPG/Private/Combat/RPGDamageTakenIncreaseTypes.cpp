#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Status, "Shared.Status.Debuff.DamageTakenIncreased");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Immunity, "Shared.Immunity.Status.DamageTakenIncrease");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Percent, "Ability.Data.Status.DamageTakenIncreasePercent");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Duration, "Ability.Data.Status.DamageTakenIncreaseSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Multiplier, "Effect.Data.Status.DamageTakenIncreasePercent");
}
bool FRPGDamageTakenIncreaseSettings::IsValid() const
{
	return FMath::IsFinite(Percent) && (Percent == 0.f || (Percent >= 1.f && Percent <= 100.f))
		&& FMath::IsFinite(Duration) && Duration >= .05f && Duration <= 30.f;
}
FGameplayTag RPGDamageTakenIncrease::StatusTag() { return TAG_Status; }
FGameplayTag RPGDamageTakenIncrease::ImmunityTag() { return TAG_Immunity; }
FGameplayTag RPGDamageTakenIncrease::MagnitudeTag() { return TAG_Multiplier; }
bool RPGDamageTakenIncrease::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGDamageTakenIncreaseSettings& Settings)
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
FRPGDamageTakenIncreaseSettings RPGDamageTakenIncrease::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGDamageTakenIncreaseSettings Result;
	const auto* Percent = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Percent) : nullptr;
	const auto* Duration = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Duration) : nullptr;
	if (!Percent || !Duration) return Result;
	Result.Percent = *Percent; Result.Duration = *Duration;
	return Result.IsValid() ? Result : FRPGDamageTakenIncreaseSettings();
}
