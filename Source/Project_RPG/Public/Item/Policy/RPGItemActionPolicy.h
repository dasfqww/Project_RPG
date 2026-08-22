#pragma once

#include "CoreMinimal.h"
#include "Type/RPGEnumTypes.h"
#include "UObject/PrimaryAssetId.h"

class URPGItemDefinition;

/** Immutable action capabilities required by authoritative item commands. */
struct PROJECT_RPG_API FRPGItemActionPolicy
{
	FPrimaryAssetId DefinitionId;
	int32 DefinitionVersion = 0;
	TSet<EEquipmentSlotType> CompatibleEquipmentSlots;
	int32 QuantityPerUse = 0;

	bool IsEquipment() const
	{
		return !CompatibleEquipmentSlots.IsEmpty();
	}

	bool IsConsumable() const
	{
		return QuantityPerUse > 0;
	}

	bool IsValid() const
	{
		if (!DefinitionId.IsValid() ||
			DefinitionVersion <= 0 ||
			QuantityPerUse < 0 ||
			(IsEquipment() && IsConsumable()))
		{
			return false;
		}

		for (const EEquipmentSlotType SlotType : CompatibleEquipmentSlots)
		{
			if (SlotType == EEquipmentSlotType::None ||
				SlotType == EEquipmentSlotType::Count)
			{
				return false;
			}
		}
		return true;
	}

	bool CanEquipInSlot(const EEquipmentSlotType SlotType) const
	{
		return IsEquipment() &&
			SlotType != EEquipmentSlotType::None &&
			SlotType != EEquipmentSlotType::Count &&
			CompatibleEquipmentSlots.Contains(SlotType);
	}
};

/** Narrow policy dependency used only by equip/use transaction commands. */
class PROJECT_RPG_API IRPGItemActionPolicyCatalog
{
public:
	virtual ~IRPGItemActionPolicyCatalog() = default;

	virtual bool TryFindActionPolicy(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemActionPolicy& OutPolicy) const = 0;
};

/** Thread-safe startup registry. Runtime commands never synchronously load assets. */
class PROJECT_RPG_API FRPGItemActionPolicyRegistry final
	: public IRPGItemActionPolicyCatalog
{
public:
	bool RegisterDefinition(const URPGItemDefinition& Definition);
	bool RegisterPolicy(const FRPGItemActionPolicy& Policy);
	void Reset();

	virtual bool TryFindActionPolicy(
		const FPrimaryAssetId& DefinitionId,
		FRPGItemActionPolicy& OutPolicy) const override;

private:
	mutable FCriticalSection CriticalSection;
	TMap<FPrimaryAssetId, FRPGItemActionPolicy> Policies;
};
