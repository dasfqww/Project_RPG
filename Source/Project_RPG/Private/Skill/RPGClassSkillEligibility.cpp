#include "Skill/RPGClassSkillEligibility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Component/Combat/PawnCombatComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"

bool RPGClassSkillEligibility::CanAssign(const AActor* Avatar, const FGameplayTag SkillTag,
	const URPGSkillDefinition* Definition, const UClass* AbilityClass, FGameplayAbilitySpecHandle Handle)
{
	const auto* Progression = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Class = Progression ? Progression->GetPlayerClassDefinition() : nullptr;
	const auto* Entry = Class ? Class->FindSkill(SkillTag) : nullptr;
	const auto* ClassOwner = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	return Entry && Entry->Definition == Definition && Entry->AbilityClass.Get() == AbilityClass
		&& Progression->IsSkillAcquired(SkillTag) && ClassOwner && ClassOwner->CanUseAbilitySpec(Handle, AbilityClass);
}

bool RPGClassSkillEligibility::CanActivate(const AActor* Avatar, const FGameplayTag SkillTag,
	const URPGSkillDefinition* Definition, const UClass* AbilityClass, FGameplayAbilitySpecHandle Handle)
{
	if (!CanAssign(Avatar, SkillTag, Definition, AbilityClass, Handle)) return false;
	const auto* Slots = Avatar->FindComponentByClass<UQuickSlotComponent>();
	if (!Slots || !Slots->IsSkillEquipped(SkillTag)) return false;
	const auto* Class = Avatar->FindComponentByClass<URPGPlayerSkillComponent>()->GetPlayerClassDefinition();
	if (Class->AllowedWeaponTypes.IsEmpty()) return true;
	const auto* Combat = Avatar->FindComponentByClass<UPawnCombatComponent>();
	const auto* Weapon = Combat ? Combat->GetCharacterCurrentEquippedWeapon() : nullptr;
	return IsValid(Weapon) && Class->AllowedWeaponTypes.Contains(Combat->GetCharacterCurrentEquippedWeaponType());
}

bool RPGClassSkillEligibility::CanChangeConfiguration(const AActor* Avatar)
{
	const auto* Progression = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	return (!Progression || Progression->IsConfigurationMutable()) && CanRestoreConfiguration(Avatar);
}

bool RPGClassSkillEligibility::CanRestoreConfiguration(const AActor* Avatar)
{
	UAbilitySystemComponent* ASC = IsValid(Avatar)
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Avatar)) : nullptr;
	if (!ASC || ASC->GetAvatarActor() != Avatar || IsDungeonGameplayRevoked(Avatar)) return false;
	FScopedAbilityListLock Lock(*ASC);
	const auto* ClassOwner = Avatar->FindComponentByClass<URPGPlayerClassComponent>();
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		if (Spec.IsActive() && (Cast<URPGGameplayAbility_SkillContainer>(Spec.Ability)
			|| (Spec.Ability && ClassOwner && ClassOwner->OwnsAbilitySpec(Spec.Handle, Spec.Ability->GetClass())))) return false;
	return true;
}
