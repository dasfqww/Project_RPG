#pragma once

#include "Item/Equipment/RPGEquipmentSnapshot.h"

/** Fixed FirstSlice recipe, derived only from validated persisted equipment. */
struct PROJECT_RPG_API FRPGEquipmentProgressionContribution
{
	float Attack = 0.f;
	float MaximumHealth = 0.f;
};

namespace RPGEquipmentProgression
{
	// Missing state contributes zero, including legacy definitions. Explicit but
	// unsupported definitions/versions/slots fail rather than silently applying.
	PROJECT_RPG_API bool TryCalculate(const FPrimaryAssetId& Definition, int32 Version,
		EEquipmentSlotType Slot, const FRPGEquipmentProgressionState& Growth,
		FRPGEquipmentProgressionContribution& Out);
	PROJECT_RPG_API bool TrySum(const TArray<FRPGEquipmentSnapshot>& Snapshots,
		EEquipState ActiveState, FRPGEquipmentProgressionContribution& Out);
}
