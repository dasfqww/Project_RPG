#pragma once

#include "CoreMinimal.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"

/** Pure mapping policy from authoritative records to the client read model. */
class PROJECT_RPG_API FRPGInventoryProjectionMapper
{
public:
	static bool BuildInventorySnapshot(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& Records,
		TArray<FRPGInventoryProjectionEntry>& OutEntries,
		FString* OutError = nullptr);
};
