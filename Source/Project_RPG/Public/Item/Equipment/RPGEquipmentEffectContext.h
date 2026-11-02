#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"

/** Server-local equipment grant lifetime; never serialized as an item property or RPC permit. */
struct PROJECT_RPG_API FRPGEquipmentEffectContext
{
	FGuid ItemId;
	FPrimaryAssetId DefinitionId;
	int32 DefinitionVersion = 0;
	FGuid GrantId;
	uint64 ActiveStateGeneration = 0;

	bool IsValid() const { return ItemId.IsValid() && DefinitionId.IsValid() && GrantId.IsValid(); }
	bool Matches(const FRPGEquipmentEffectContext& Other) const
	{
		return IsValid() && Other.IsValid() && ItemId == Other.ItemId && DefinitionId == Other.DefinitionId
			&& DefinitionVersion == Other.DefinitionVersion && GrantId == Other.GrantId
			&& ActiveStateGeneration == Other.ActiveStateGeneration;
	}
};
