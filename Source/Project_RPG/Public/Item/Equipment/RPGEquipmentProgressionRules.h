#pragma once

#include "Item/Equipment/RPGEquipmentSnapshot.h"

/** Fixed FirstSlice recipe, derived only from validated persisted equipment. */
struct PROJECT_RPG_API FRPGEquipmentProgressionContribution
{
	float Attack = 0.f;
	float MaximumHealth = 0.f;
	float CooldownReductionPercent = 0.f;
	float DamageReductionPercent = 0.f;

	bool operator==(const FRPGEquipmentProgressionContribution&) const = default;
	bool IsEmpty() const { return *this == FRPGEquipmentProgressionContribution{}; }
};

namespace RPGEquipmentProgression
{
	// Missing state contributes zero, including legacy definitions. Explicit but
	// unsupported definitions/versions/slots fail rather than silently applying.
	PROJECT_RPG_API bool TryCalculate(const FPrimaryAssetId& Definition, int32 Version,
		EEquipmentSlotType Slot, const FRPGEquipmentProgressionState& Growth,
		FRPGEquipmentProgressionContribution& Out);
	/** Read-only value preview for inventory and mail, where the record has no equipment slot. */
	PROJECT_RPG_API bool TryPreview(const FPrimaryAssetId& Definition, int32 Version,
		const FRPGEquipmentProgressionState& Growth, FRPGEquipmentProgressionContribution& Out);
	PROJECT_RPG_API bool TrySum(const TArray<FRPGEquipmentSnapshot>& Snapshots,
		EEquipState ActiveState, FRPGEquipmentProgressionContribution& Out);
}
