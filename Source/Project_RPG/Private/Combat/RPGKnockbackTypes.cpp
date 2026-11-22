#include "Combat/RPGKnockbackTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Knockback, "Shared.Status.Control.Knockback");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KnockbackImmunity, "Shared.Immunity.Control.Knockback");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KnockbackDistance, "Ability.Data.Control.KnockbackDistance");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KnockbackDuration, "Ability.Data.Control.KnockbackSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_AirborneHeight, "Ability.Data.Control.AirborneHeight");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_DownDuration, "Ability.Data.Control.DownSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MaxDownDuration, "Ability.Data.Control.MaxDownSeconds");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_GetUpDuration, "Ability.Data.Control.GetUpSeconds");
}

bool FRPGKnockbackSettings::IsValid() const
{
	return FMath::IsFinite(Distance) && Distance >= 0.f && Distance <= 1000.f
		&& FMath::IsFinite(Duration) && Duration >= .1f && Duration <= 2.f
		&& FMath::IsFinite(Height) && (Height == 0.f || (Height >= 50.f && Height <= 600.f))
		&& (Height > 0.f || Distance / Duration <= 2500.f)
		&& FMath::IsFinite(DownDuration) && DownDuration >= .1f && DownDuration <= 4.f
		&& (MaxDownDuration == 0.f || (FMath::IsFinite(MaxDownDuration) && MaxDownDuration >= DownDuration && MaxDownDuration <= 4.f))
		&& FMath::IsFinite(GetUpDuration) && GetUpDuration >= .1f && GetUpDuration <= 3.f;
}

FGameplayTag RPGKnockback::StatusTag() { return TAG_Knockback; }
FGameplayTag RPGKnockback::ImmunityTag() { return TAG_KnockbackImmunity; }

bool RPGKnockback::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGKnockbackSettings& Settings)
{
	if (!Settings.IsValid()) return false;
	Spec.SetByCallerTagMagnitudes.Remove(TAG_KnockbackDistance);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_KnockbackDuration);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_AirborneHeight);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_DownDuration);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_MaxDownDuration);
	Spec.SetByCallerTagMagnitudes.Remove(TAG_GetUpDuration);
	if (Settings.IsEnabled())
	{
		Spec.SetByCallerTagMagnitudes.Add(TAG_KnockbackDistance, Settings.Distance);
		Spec.SetByCallerTagMagnitudes.Add(TAG_KnockbackDuration, Settings.Duration);
		if (Settings.IsAirborne())
		{
			Spec.SetByCallerTagMagnitudes.Add(TAG_AirborneHeight, Settings.Height);
			Spec.SetByCallerTagMagnitudes.Add(TAG_DownDuration, Settings.DownDuration);
			if (Settings.MaxDownDuration > 0.f)
				Spec.SetByCallerTagMagnitudes.Add(TAG_MaxDownDuration, Settings.MaxDownDuration);
			Spec.SetByCallerTagMagnitudes.Add(TAG_GetUpDuration, Settings.GetUpDuration);
		}
	}
	return true;
}

FRPGKnockbackSettings RPGKnockback::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGKnockbackSettings Result;
	if (!Spec) return Result;
	const float* Distance = Spec->SetByCallerTagMagnitudes.Find(TAG_KnockbackDistance);
	const float* Duration = Spec->SetByCallerTagMagnitudes.Find(TAG_KnockbackDuration);
	if (!Distance || !Duration) return Result;
	Result.Distance = *Distance; Result.Duration = *Duration;
	const float* Height = Spec->SetByCallerTagMagnitudes.Find(TAG_AirborneHeight);
	const float* Down = Spec->SetByCallerTagMagnitudes.Find(TAG_DownDuration);
	const float* MaxDown = Spec->SetByCallerTagMagnitudes.Find(TAG_MaxDownDuration);
	const float* GetUp = Spec->SetByCallerTagMagnitudes.Find(TAG_GetUpDuration);
	if (Height || Down || MaxDown || GetUp)
	{
		if (!Height || !Down || !GetUp || *Height <= 0.f) return FRPGKnockbackSettings();
		Result.Height = *Height; Result.DownDuration = *Down; Result.GetUpDuration = *GetUp;
		if (MaxDown) Result.MaxDownDuration = *MaxDown;
	}
	return Result.IsValid() ? Result : FRPGKnockbackSettings();
}
