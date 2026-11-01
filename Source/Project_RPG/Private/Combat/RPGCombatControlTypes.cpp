#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGAirborneTypes.h"
#include "Combat/RPGStaggerTypes.h"

#include "Ability/RPGGameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"
#include "RPGGameplayTags.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Stun, "Shared.Status.Control.Stunned");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StunImmunity, "Shared.Immunity.Control.Stun");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StunDuration, "Ability.Data.Control.StunSeconds");
}

FGameplayTag RPGCombatControl::StunTag() { return TAG_Stun; }
FGameplayTag RPGCombatControl::StunImmunityTag() { return TAG_StunImmunity; }

bool RPGCombatControl::IsStunned(const AActor* Actor)
{
	const auto* ASC = IsValid(Actor) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr;
	return ASC && ASC->GetAvatarActor() == Actor && ASC->HasMatchingGameplayTag(TAG_Stun);
}

bool RPGCombatControl::IsStunAffectedAbility(const UGameplayAbility* Ability)
{
	const auto* RPG = Cast<URPGGameplayAbility>(Ability);
	return RPG && RPG->GetNetExecutionPolicy() != EGameplayAbilityNetExecutionPolicy::LocalOnly
		&& !RPG->ActivatesOnGrant() && !RPG->GetAssetTags().HasTag(RPGGameplayTags::Shared_Ability_Death);
}

bool RPGCombatControl::IsActionBlocked(const AActor* Actor)
{
	const auto* ASC = IsValid(Actor) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor)) : nullptr;
	return ASC && ASC->GetAvatarActor() == Actor
		&& (ASC->HasMatchingGameplayTag(TAG_Stun) || ASC->HasMatchingGameplayTag(RPGStagger::StatusTag()) || ASC->HasMatchingGameplayTag(RPGKnockback::StatusTag())
			|| ASC->HasMatchingGameplayTag(RPGAirborne::SequenceTag()));
}

bool RPGCombatControl::IsValidStunDuration(float Seconds)
{
	return FMath::IsFinite(Seconds) && (Seconds == 0.f || (Seconds >= .05f && Seconds <= 10.f));
}

bool RPGCombatControl::SetGrantedStunDuration(FGameplayAbilitySpec& Spec, float Seconds)
{
	if (!IsValidStunDuration(Seconds)) return false;
	if (Seconds > 0.f) Spec.SetByCallerTagMagnitudes.Add(TAG_StunDuration, Seconds);
	else Spec.SetByCallerTagMagnitudes.Remove(TAG_StunDuration);
	return true;
}

float RPGCombatControl::GetGrantedStunDuration(const FGameplayAbilitySpec* Spec)
{
	const float* Value = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_StunDuration) : nullptr;
	return Value && IsValidStunDuration(*Value) ? *Value : 0.f;
}
