#include "Combat/RPGBurnTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Burn, "Shared.Status.Damage.Burning");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Immunity, "Shared.Immunity.Status.Burn");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Damage, "Ability.Data.Status.BurnDamage");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Interval, "Ability.Data.Status.BurnInterval");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Duration, "Ability.Data.Status.BurnSeconds");
}
bool FRPGBurnSettings::IsValid() const
{
	return TickDamage >= 0 && TickDamage <= 10000 && FMath::IsFinite(Interval) && Interval >= .5f && Interval <= 5.f
		&& FMath::IsFinite(Duration) && Duration > Interval && Duration <= 30.f;
}
FGameplayTag RPGBurn::StatusTag() { return TAG_Burn; }
FGameplayTag RPGBurn::ImmunityTag() { return TAG_Immunity; }
bool RPGBurn::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGBurnSettings& Settings)
{
	if (!Settings.IsValid()) return false;
	for (FGameplayTag Tag : {TAG_Damage.GetTag(), TAG_Interval.GetTag(), TAG_Duration.GetTag()}) Spec.SetByCallerTagMagnitudes.Remove(Tag);
	if (Settings.IsEnabled())
	{
		Spec.SetByCallerTagMagnitudes.Add(TAG_Damage, float(Settings.TickDamage));
		Spec.SetByCallerTagMagnitudes.Add(TAG_Interval, Settings.Interval);
		Spec.SetByCallerTagMagnitudes.Add(TAG_Duration, Settings.Duration);
	}
	return true;
}
FRPGBurnSettings RPGBurn::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGBurnSettings Result;
	const float* Damage = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Damage) : nullptr;
	const float* Interval = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Interval) : nullptr;
	const float* Duration = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Duration) : nullptr;
	if (!Damage || !Interval || !Duration || !FMath::IsFinite(*Damage) || *Damage < 1.f || *Damage > 10000.f
		|| FMath::FloorToFloat(*Damage) != *Damage) return Result;
	Result.TickDamage = int32(*Damage); Result.Interval = *Interval; Result.Duration = *Duration;
	return Result.IsValid() ? Result : FRPGBurnSettings();
}
