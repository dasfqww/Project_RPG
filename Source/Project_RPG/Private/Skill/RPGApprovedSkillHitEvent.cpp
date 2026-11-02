#include "Skill/RPGApprovedSkillHitEvent.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "NativeGameplayTags.h"
#include "GameFramework/Actor.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_RPG_ApprovedSkillHit, "Shared.Event.Skill.ApprovedHit");

FGameplayTag URPGApprovedSkillHitEvent::EventTag() { return TAG_RPG_ApprovedSkillHit; }
URPGGameplayAbility_SkillContainer* URPGApprovedSkillHitEvent::GetSkill() const { return Skill.Get(); }

bool URPGApprovedSkillHitEvent::IsCurrent() const
{
	const auto* Ability = Skill.Get();
	AActor* Actor = Avatar.Get();
	return Ability && Actor && Actor->HasAuthority() && Ability->IsActive()
		&& Ability->GetAvatarActorFromActorInfo() == Actor
		&& Ability->GetSkillActivationSerial() == ActivationSerial
		&& Lifetime.IsCurrent(Actor, Actor) && Equipment.IsValid();
}
