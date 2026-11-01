#include "Combat/RPGHitReactionTypes.h"
#include "GameplayAbilitySpec.h"
#include "NativeGameplayTags.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(DisableReaction, "Ability.Data.HitReaction.None");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(CosmeticReaction, "Ability.Data.HitReaction.Cosmetic");
}

bool RPGHitReaction::IsValidPolicy(ERPGAttackHitReactionPolicy Policy)
{
	return Policy == ERPGAttackHitReactionPolicy::Legacy || Policy == ERPGAttackHitReactionPolicy::None
		|| Policy == ERPGAttackHitReactionPolicy::Cosmetic;
}

bool RPGHitReaction::SetGrantedPolicy(FGameplayAbilitySpec& Spec, ERPGAttackHitReactionPolicy Policy)
{
	if (!IsValidPolicy(Policy)) return false;
	FGameplayTagContainer& Tags = Spec.GetDynamicSpecSourceTags();
	Tags.RemoveTag(DisableReaction);
	Tags.RemoveTag(CosmeticReaction);
	if (Policy == ERPGAttackHitReactionPolicy::None) Tags.AddTag(DisableReaction);
	if (Policy == ERPGAttackHitReactionPolicy::Cosmetic) Tags.AddTag(CosmeticReaction);
	return true;
}

ERPGAttackHitReactionPolicy RPGHitReaction::GetGrantedPolicy(const FGameplayAbilitySpec* Spec)
{
	if (!Spec) return ERPGAttackHitReactionPolicy::Legacy;
	const auto& Tags = Spec->GetDynamicSpecSourceTags();
	// Conflicting metadata cannot fall back to an interrupting legacy reaction.
	if (Tags.HasTagExact(DisableReaction)) return ERPGAttackHitReactionPolicy::None;
	return Tags.HasTagExact(CosmeticReaction) ? ERPGAttackHitReactionPolicy::Cosmetic : ERPGAttackHitReactionPolicy::Legacy;
}

ERPGHitReactionDirection RPGHitReaction::ResolveDirection(
	const FVector& VictimForward, const FVector& VictimToSource)
{
	if (VictimForward.ContainsNaN() || VictimToSource.ContainsNaN())
		return ERPGHitReactionDirection::Front;
	const FVector Forward = VictimForward.GetSafeNormal2D();
	const FVector TowardSource = VictimToSource.GetSafeNormal2D();
	if (Forward.IsNearlyZero() || TowardSource.IsNearlyZero())
		return ERPGHitReactionDirection::Front;
	const double Angle = FMath::RadiansToDegrees(FMath::Atan2(
		FVector::CrossProduct(Forward, TowardSource).Z, FVector::DotProduct(Forward, TowardSource)));
	if (Angle >= -45.0 && Angle <= 45.0) return ERPGHitReactionDirection::Front;
	if (Angle > 45.0 && Angle <= 135.0) return ERPGHitReactionDirection::Right;
	if (Angle < -45.0 && Angle >= -135.0) return ERPGHitReactionDirection::Left;
	return ERPGHitReactionDirection::Back;
}

bool RPGHitReaction::IsNewerSequence(const uint32 Candidate, const uint32 Previous)
{
	const uint32 Distance = Candidate - Previous;
	return Distance != 0 && Distance < 0x80000000u;
}
