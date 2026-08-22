#include "Item/Definition/RPGItemDefinitionFragments.h"

#include "Ability/RPGAbilitySet.h"
#include "GameplayEffect.h"
#include "Item/RPGItemRuntimeTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemDefinitionFragments)

bool FRPGItemStatRange::IsValid() const
{
	return StatTag.IsValid() &&
		FMath::IsFinite(Minimum) &&
		FMath::IsFinite(Maximum) &&
		Minimum <= Maximum;
}

float FRPGItemStatRange::Roll(FRandomStream& RandomStream) const
{
	return FMath::IsNearlyEqual(Minimum, Maximum)
		? Minimum
		: RandomStream.FRandRange(Minimum, Maximum);
}

void URPGItemStatDefinitionFragment::BuildInstanceState(
	FRPGItemInstanceStateBuilder& Builder,
	FRandomStream& RandomStream) const
{
	for (const FRPGItemStatRange& StatRange : StatRanges)
	{
		if (StatRange.IsValid())
		{
			Builder.AddStatValue(
				StatRange.StatTag,
				StatRange.Roll(RandomStream));
		}
	}
}

#if WITH_EDITOR
EDataValidationResult URPGItemStatDefinitionFragment::ValidateDefinition(
	const URPGItemDefinition& Definition,
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	if (StatRanges.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "EmptyStatRanges",
			"An Item Stat fragment must contain at least one stat range."));
		return EDataValidationResult::Invalid;
	}

	TSet<FGameplayTag> SeenTags;
	for (const FRPGItemStatRange& StatRange : StatRanges)
	{
		if (!StatRange.IsValid())
		{
			Context.AddError(NSLOCTEXT("RPGItemDefinition", "InvalidStatRange",
				"An Item Stat range has an invalid tag or numeric range."));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		if (SeenTags.Contains(StatRange.StatTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("RPGItemDefinition", "DuplicateStatTag",
					"Stat tag {0} is duplicated in an Item Stat fragment."),
				FText::FromName(StatRange.StatTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		SeenTags.Add(StatRange.StatTag);
	}
	return Result;
}
#endif

bool URPGItemEquipmentDefinitionFragment::CanEquipInSlot(
	const EEquipmentSlotType SlotType) const
{
	return SlotType != EEquipmentSlotType::None &&
		SlotType != EEquipmentSlotType::Count &&
		CompatibleSlots.Contains(SlotType);
}

#if WITH_EDITOR
EDataValidationResult
URPGItemEquipmentDefinitionFragment::ValidateDefinition(
	const URPGItemDefinition& Definition,
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	if (Definition.ItemCategory != EItemCategory::Equip)
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "EquipmentCategoryMismatch",
			"An Equipment fragment requires the Equip item category."));
		Result = EDataValidationResult::Invalid;
	}
	if (Definition.GetMaxStackSize() != 1)
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "StackableEquipment",
			"Equipment items must have a Max Stack Size of one."));
		Result = EDataValidationResult::Invalid;
	}
	if (CompatibleSlots.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "NoCompatibleSlots",
			"An Equipment fragment must declare at least one compatible slot."));
		Result = EDataValidationResult::Invalid;
	}
	for (const URPGAbilitySet* AbilitySet : GrantedAbilitySets)
	{
		if (!IsValid(AbilitySet))
		{
			Context.AddError(NSLOCTEXT("RPGItemDefinition", "NullAbilitySet",
				"Granted Ability Sets cannot contain null entries."));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

#if WITH_EDITOR
EDataValidationResult
URPGItemConsumableDefinitionFragment::ValidateDefinition(
	const URPGItemDefinition& Definition,
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	if (Definition.ItemCategory != EItemCategory::Consume)
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "ConsumableCategoryMismatch",
			"A Consumable fragment requires the Consume item category."));
		Result = EDataValidationResult::Invalid;
	}
	if (!GameplayEffect)
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "MissingConsumeEffect",
			"A Consumable fragment must specify a Gameplay Effect."));
		Result = EDataValidationResult::Invalid;
	}
	if (QuantityPerUse < 1)
	{
		Context.AddError(NSLOCTEXT("RPGItemDefinition", "InvalidQuantityPerUse",
			"Quantity Per Use must be at least one."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
