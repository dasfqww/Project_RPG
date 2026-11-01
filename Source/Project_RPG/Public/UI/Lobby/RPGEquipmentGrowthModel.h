#pragma once

#include "Progression/RPGEquipmentMutationTypes.h"
#include "Item/Equipment/RPGEquipmentProgressionRules.h"

/** Presentation of the current FirstSlice contract, never a grant or a server price. */
struct PROJECT_RPG_API FRPGEquipmentGrowthChoice
{
	FGuid ItemId;
	FText Label;
	EEquipmentSlotType Slot = EEquipmentSlotType::None;
	int32 Level = 0;
};

struct PROJECT_RPG_API FRPGEquipmentGrowthPreview
{
	bool bValid = false;
	ERPGEquipmentMutationOperation Operation = ERPGEquipmentMutationOperation::Count;
	FRPGEquipmentItemExpectation Source, Target;
	TArray<FRPGEquipmentCostExpectation> Costs;
	int32 OreCost = 0;
	FText Text;
	/** Confirmation must still refer to these exact IDs, revisions and levels. */
	bool HasSameIntent(const FRPGEquipmentGrowthPreview& Other) const;
};

namespace RPGEquipmentGrowthUI
{
	PROJECT_RPG_API bool CanStartNew(const FRPGEquipmentMutationView& View);
	PROJECT_RPG_API bool CanRetry(const FRPGEquipmentMutationView& View);
	PROJECT_RPG_API TArray<FRPGEquipmentGrowthChoice> Choices(const FRPGEquipmentMutationView& View, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Upgrade(const FRPGEquipmentMutationView& View, FGuid Item, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Transfer(const FRPGEquipmentMutationView& View, FGuid Source, FGuid Target, FDateTime Now);
	PROJECT_RPG_API FText Status(const FRPGEquipmentMutationView& View);
}
