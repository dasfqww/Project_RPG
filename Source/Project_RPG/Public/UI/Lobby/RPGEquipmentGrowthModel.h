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
	int32 TokenCost = 0;
	FString TargetDefinitionName;
	FText Text;
	/** Confirmation must still refer to these exact IDs, revisions and levels. */
	bool HasSameIntent(const FRPGEquipmentGrowthPreview& Other) const;
};

/** Current lobby guards only; an explanation never grants server authority. */
struct PROJECT_RPG_API FRPGEquipmentGrowthAccess
{
	bool bHome = false;
	bool bContextReady = false;
	bool bTutorialActive = false;
	bool bProgressionBusy = false;
	bool bConfigurationBusy = false;
	bool bConfigurationUnresolved = false;
	bool bActiveSession = false;
	bool bSteamParty = false;
	bool bTravelUnresolved = false;
	bool bUnsavedConfiguration = false;
};

struct PROJECT_RPG_API FRPGEquipmentGrowthAccessResult
{
	bool bAllowed = false;
	FText Reason;
};

namespace RPGEquipmentGrowthUI
{
	PROJECT_RPG_API FRPGEquipmentGrowthAccessResult EvaluateAccess(const FRPGEquipmentGrowthAccess& Access, bool bWrite, bool bRetry);
	/** Presentation only. The typed bAllowed flag, never localized text, gates execution. */
	PROJECT_RPG_API FText AccessReason(const FRPGEquipmentGrowthAccess& Access, bool bWrite, bool bRetry);
	/** Stable exact target IDs; indices are presentation only. */
	PROJECT_RPG_API TArray<TPair<FString,FText>> CraftTargets();
	PROJECT_RPG_API bool CanStartNew(const FRPGEquipmentMutationView& View);
	PROJECT_RPG_API bool CanRetry(const FRPGEquipmentMutationView& View);
	PROJECT_RPG_API TArray<FRPGEquipmentGrowthChoice> Choices(const FRPGEquipmentMutationView& View, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Upgrade(const FRPGEquipmentMutationView& View, FGuid Item, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Transfer(const FRPGEquipmentMutationView& View, FGuid Source, FGuid Target, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Craft(const FRPGEquipmentMutationView& View, const FString& TargetDefinitionName, FDateTime Now);
	PROJECT_RPG_API FRPGEquipmentGrowthPreview Dismantle(const FRPGEquipmentMutationView& View, FGuid Item, FDateTime Now);
	PROJECT_RPG_API FText Status(const FRPGEquipmentMutationView& View, const FText& AccessReason = FText::GetEmpty());
}
