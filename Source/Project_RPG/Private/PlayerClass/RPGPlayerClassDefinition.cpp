#include "PlayerClass/RPGPlayerClassDefinition.h"

#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FPrimaryAssetType URPGPlayerClassDefinition::PrimaryAssetType(TEXT("RPGPlayerClassDefinition"));

FPrimaryAssetId URPGPlayerClassDefinition::GetPrimaryAssetId() const
{
	return ClassId.IsNone() ? FPrimaryAssetId() : FPrimaryAssetId(PrimaryAssetType, ClassId);
}

const FRPGPlayerClassSkill* URPGPlayerClassDefinition::FindSkill(const FGameplayTag SkillTag) const
{
	if (!SkillTag.IsValid()) return nullptr;
	const FRPGPlayerClassSkill* Result = nullptr;
	for (const FRPGPlayerClassSkill& Entry : Skills)
	{
		if (!Entry.Definition || Entry.Definition->SkillTag != SkillTag) continue;
		if (Result) return nullptr; // Never resolve ambiguous data by array order.
		Result = &Entry;
	}
	return Result;
}

bool URPGPlayerClassDefinition::ValidateConfiguration(FText& OutError) const
{
	OutError = FText::GetEmpty();
	const auto Fail = [&OutError](const TCHAR* Message)
	{
		OutError = FText::FromString(Message);
		return false;
	};
	if (ClassId.IsNone()) return Fail(TEXT("A stable ClassId is required."));
	const auto SupportsOwnedGrant = [](const URPGGameplayAbility* Ability)
	{
		return Ability && !Ability->ActivatesOnGrant()
			&& (Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerActor
				|| Ability->GetInstancingPolicy() == EGameplayAbilityInstancingPolicy::InstancedPerExecution)
			&& (Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalPredicted
				|| Ability->GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::ServerOnly);
	};
	TSet<FGameplayTag> Tags;
	TSet<UClass*> Classes;
	for (const FRPGPlayerClassSkill& Entry : Skills)
	{
		const auto* Ability = Entry.AbilityClass ? Entry.AbilityClass.GetDefaultObject() : nullptr;
		if (!Entry.Definition || !Entry.Definition->SkillTag.IsValid() || !SupportsOwnedGrant(Ability)
			|| Entry.AbilityClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| Ability->GetSkillDefinition() != Entry.Definition)
			return Fail(TEXT("Each class skill must bind a concrete ability to its exact SkillDefinition."));
		if (Tags.Contains(Entry.Definition->SkillTag) || Classes.Contains(Entry.AbilityClass.Get()))
			return Fail(TEXT("Class skills must have unique SkillTags and ability classes."));
		Tags.Add(Entry.Definition->SkillTag);
		Classes.Add(Entry.AbilityClass.Get());
	}
	if (IdentityAbility)
	{
		const auto* Identity = IdentityAbility.GetDefaultObject();
		if (!SupportsOwnedGrant(Identity)
			|| IdentityAbility->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| IdentityAbility->IsChildOf(URPGGameplayAbility_SkillContainer::StaticClass())
			|| Classes.Contains(IdentityAbility.Get()))
			return Fail(TEXT("Identity must be a distinct instanced server gameplay ability. Automatic grant activation is not supported by this transaction."));
	}
	else if (IdentityInputTag.IsValid()) return Fail(TEXT("Identity input requires an identity ability."));
	if (DefaultSkillSlots.Num() > MaxCombatSkills)
		return Fail(TEXT("A class supports at most eight default skill slots."));
	TSet<FGameplayTag> Slotted;
	for (const FGameplayTag Tag : DefaultSkillSlots)
	{
		if (!Tag.IsValid()) continue;
		if (!Tags.Contains(Tag) || Slotted.Contains(Tag))
			return Fail(TEXT("Default slots must reference unique skills from this class."));
		Slotted.Add(Tag);
	}
	TSet<ERPGGladiatorWeaponType> Weapons;
	for (const ERPGGladiatorWeaponType Type : AllowedWeaponTypes)
	{
		if (static_cast<uint8>(Type) >= static_cast<uint8>(ERPGGladiatorWeaponType::Count)
			|| Weapons.Contains(Type))
			return Fail(TEXT("Allowed weapon types must be valid and unique."));
		Weapons.Add(Type);
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult URPGPlayerClassDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FText Error;
	if (!ValidateConfiguration(Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}
	// Reuse the existing skill asset validation, including execution/targeting configuration.
	for (const FRPGPlayerClassSkill& Entry : Skills)
		if (Entry.Definition->IsDataValid(Context) == EDataValidationResult::Invalid)
			Result = EDataValidationResult::Invalid;
	return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif
