#include "Item/Policy/RPGItemActionPolicy.h"

#include "Ability/RPGAbilitySet.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "GameplayEffect.h"
#include "Misc/ScopeLock.h"

bool FRPGItemActionPolicyRegistry::RegisterDefinition(
	const URPGItemDefinition& Definition)
{
	FRPGItemActionPolicy Policy;
	Policy.DefinitionId = Definition.GetPrimaryAssetId();
	Policy.DefinitionVersion = Definition.GetDefinitionVersion();

	const URPGItemEquipmentDefinitionFragment* Equipment =
		Definition.FindFragmentByClass<
			URPGItemEquipmentDefinitionFragment>();
	const URPGItemConsumableDefinitionFragment* Consumable =
		Definition.FindFragmentByClass<
			URPGItemConsumableDefinitionFragment>();
	if ((Equipment != nullptr) !=
			(Definition.ItemCategory == EItemCategory::Equip) ||
		(Consumable != nullptr) !=
			(Definition.ItemCategory == EItemCategory::Consume))
	{
		return false;
	}

	if (Equipment)
	{
		if (Definition.GetMaxStackSize() != 1 ||
			Equipment->CompatibleSlots.IsEmpty())
		{
			return false;
		}
		Policy.CompatibleEquipmentSlots = Equipment->CompatibleSlots;
		for (const URPGAbilitySet* AbilitySet :
			Equipment->GrantedAbilitySets)
		{
			if (!IsValid(AbilitySet))
			{
				return false;
			}
		}
	}

	if (Consumable)
	{
		if (Consumable->QuantityPerUse < 1 ||
			!Consumable->GameplayEffect)
		{
			return false;
		}
		Policy.QuantityPerUse = Consumable->QuantityPerUse;
	}

	return RegisterPolicy(Policy);
}

bool FRPGItemActionPolicyRegistry::RegisterPolicy(
	const FRPGItemActionPolicy& Policy)
{
	if (!Policy.IsValid())
	{
		return false;
	}

	FScopeLock Lock(&CriticalSection);
	Policies.Add(Policy.DefinitionId, Policy);
	return true;
}

void FRPGItemActionPolicyRegistry::Reset()
{
	FScopeLock Lock(&CriticalSection);
	Policies.Reset();
}

bool FRPGItemActionPolicyRegistry::TryFindActionPolicy(
	const FPrimaryAssetId& DefinitionId,
	FRPGItemActionPolicy& OutPolicy) const
{
	FScopeLock Lock(&CriticalSection);
	const FRPGItemActionPolicy* Policy = Policies.Find(DefinitionId);
	if (!Policy)
	{
		return false;
	}

	OutPolicy = *Policy;
	return true;
}
