#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRepository.h"

class IRPGItemDefinitionCatalog;
class URPGItemDefinition;

/** Server-issued identity is retained across retries of the same world pickup. */
struct RPGINVENTORYRUNTIME_API FRPGItemPickupGrantRequest
{
	FGuid RequestId;
	FGuid ItemId;
	FRPGItemOwnerRef Owner;
	int32 Quantity = 0;
	int32 GenerationSeed = 0;
	FString CreationSource;
	/** Server-local exclusions for initial planning only; never serialized into a committed request. */
	TSet<int32> ReservedInventorySlots;
};

/** Pure planning boundary; it does not claim or despawn the world actor. */
class RPGINVENTORYRUNTIME_API FRPGItemPickupGrantPlanner final
{
public:
	static bool TryPlan(
		const FRPGItemPickupGrantRequest& Request,
		const URPGItemDefinition& Definition,
		const IRPGItemRecordSource& Records,
		const IRPGItemDefinitionCatalog& Definitions,
		FRPGItemRepositoryCommitRequest& OutCommit,
		FString* OutError = nullptr);
};
