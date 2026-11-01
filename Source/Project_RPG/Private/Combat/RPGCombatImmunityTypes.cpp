#include "Combat/RPGCombatImmunityTypes.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGAirborneTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_HitReaction, "Shared.Immunity.HitReaction");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Displacement, "Shared.Immunity.Displacement");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_StatusEffects, "Shared.Immunity.StatusEffects");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_ActivationFlags, "Ability.Data.Immunity.Flags");
	FGameplayTag Leaf(ERPGCombatEffectKind Kind)
	{
		switch (Kind)
		{
		case ERPGCombatEffectKind::HitReaction:
		case ERPGCombatEffectKind::Stagger: return TAG_HitReaction;
		case ERPGCombatEffectKind::Stun: return RPGCombatControl::StunImmunityTag();
		case ERPGCombatEffectKind::Knockback: return RPGKnockback::ImmunityTag();
		case ERPGCombatEffectKind::Airborne: return RPGAirborne::ImmunityTag();
		case ERPGCombatEffectKind::Slow: return RPGSlow::ImmunityTag();
		case ERPGCombatEffectKind::Burn: return RPGBurn::ImmunityTag();
		case ERPGCombatEffectKind::DefenseReduction: return RPGDefenseReduction::ImmunityTag();
		case ERPGCombatEffectKind::DamageTakenIncrease: return RPGDamageTakenIncrease::ImmunityTag();
		default: return {};
		}
	}
	FGameplayTag Group(ERPGCombatEffectKind Kind)
	{
		if (Kind == ERPGCombatEffectKind::Knockback || Kind == ERPGCombatEffectKind::Airborne) return TAG_Displacement;
		if (Kind == ERPGCombatEffectKind::Stun || Kind == ERPGCombatEffectKind::Slow || Kind == ERPGCombatEffectKind::Burn || Kind == ERPGCombatEffectKind::DefenseReduction || Kind == ERPGCombatEffectKind::DamageTakenIncrease) return TAG_StatusEffects;
		return {};
	}
}

FGameplayTag RPGCombatImmunity::HitReactionTag() { return TAG_HitReaction; }

bool FRPGCombatImmunitySettings::Protects(ERPGCombatEffectKind Kind) const
{
	if (!IsValid()) return false;
	int32 Mask = 0;
	switch (Kind)
	{
	case ERPGCombatEffectKind::HitReaction:
	case ERPGCombatEffectKind::Stagger: Mask = 1; break;
	case ERPGCombatEffectKind::Stun: Mask = 4 | 8; break;
	case ERPGCombatEffectKind::Knockback: Mask = 2 | 16; break;
	case ERPGCombatEffectKind::Airborne: Mask = 2 | 32; break;
	case ERPGCombatEffectKind::Slow: Mask = 4 | 64; break;
	case ERPGCombatEffectKind::Burn: Mask = 4 | 128; break;
	case ERPGCombatEffectKind::DefenseReduction: Mask = 4; break;
	case ERPGCombatEffectKind::DamageTakenIncrease: Mask = 4; break;
	default: return false;
	}
	return (Flags & Mask) != 0;
}

FGameplayTagContainer FRPGCombatImmunitySettings::GetGrantedTags() const
{
	FGameplayTagContainer Result;
	if (!IsValid()) return Result;
	const FGameplayTag Tags[] = {TAG_HitReaction, TAG_Displacement, TAG_StatusEffects,
		RPGCombatControl::StunImmunityTag(), RPGKnockback::ImmunityTag(), RPGAirborne::ImmunityTag(), RPGSlow::ImmunityTag(), RPGBurn::ImmunityTag()};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
		if ((Flags & (1 << Index)) != 0) Result.AddTag(Tags[Index]);
	return Result;
}

bool RPGCombatImmunity::HasTags(const UAbilitySystemComponent* ASC, ERPGCombatEffectKind Kind)
{
	if (!ASC) return false;
	const FGameplayTag Individual = Leaf(Kind), Category = Group(Kind);
	return (Individual.IsValid() && ASC->HasMatchingGameplayTag(Individual))
		|| (Category.IsValid() && ASC->HasMatchingGameplayTag(Category));
}

bool RPGCombatImmunity::IsImmune(const AActor* Actor, ERPGCombatEffectKind Kind)
{
	if (!IsValid(Actor)) return false;
	if (const auto* Control = Actor->FindComponentByClass<URPGCombatControlComponent>()) return Control->IsImmuneTo(Kind);
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Actor));
	return ASC && ASC->GetAvatarActor() == Actor && HasTags(ASC, Kind);
}

bool RPGCombatImmunity::SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGCombatImmunitySettings& Settings)
{
	if (!Settings.IsValid()) return false;
	Spec.SetByCallerTagMagnitudes.Add(TAG_ActivationFlags, float(Settings.Flags));
	return true;
}

FRPGCombatImmunitySettings RPGCombatImmunity::GetGrantedSettings(const FGameplayAbilitySpec* Spec)
{
	FRPGCombatImmunitySettings Settings;
	const float* Value = Spec ? Spec->SetByCallerTagMagnitudes.Find(TAG_ActivationFlags) : nullptr;
	// -1 means no per-grant override; -2 is invalid and must not create protection.
	Settings.Flags = !Value ? -1 : FMath::IsFinite(*Value) && *Value >= 0.f && *Value <= 255.f && FMath::FloorToFloat(*Value) == *Value ? int32(*Value) : -2;
	return Settings;
}
