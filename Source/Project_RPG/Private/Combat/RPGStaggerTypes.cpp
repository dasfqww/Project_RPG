#include "Combat/RPGStaggerTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGAirborneTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Stagger, "Shared.Status.Control.Staggered");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Duration, "Ability.Data.Control.StaggerSeconds");
}
FGameplayTag RPGStagger::StatusTag() { return TAG_Stagger; }
bool RPGStagger::IsStaggered(const AActor* Actor)
{
	const auto* ASC = IsValid(Actor) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr;
	return ASC && ASC->GetAvatarActor() == Actor && ASC->HasMatchingGameplayTag(TAG_Stagger);
}
bool RPGStagger::HasHigherControl(const AActor* Actor)
{
	const auto* ASC = IsValid(Actor) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr;
	return ASC && ASC->GetAvatarActor() == Actor && (ASC->HasMatchingGameplayTag(RPGCombatControl::StunTag())
		|| ASC->HasMatchingGameplayTag(RPGKnockback::StatusTag()) || ASC->HasMatchingGameplayTag(RPGAirborne::SequenceTag()));
}
bool RPGStagger::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGStaggerSettings& Settings)
{
	if (!Settings.IsValid()) return false;
	Spec.SetByCallerTagMagnitudes.Remove(TAG_Duration);
	if (Settings.IsEnabled()) Spec.SetByCallerTagMagnitudes.Add(TAG_Duration, Settings.Duration);
	return true;
}
FRPGStaggerSettings RPGStagger::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGStaggerSettings Result;
	if (const float* Value = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_Duration) : nullptr) Result.Duration = *Value;
	return Result.IsValid() ? Result : FRPGStaggerSettings();
}
