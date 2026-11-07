#pragma once

#include "CoreMinimal.h"
#include "Item/RPGInventoryTypes.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"

/** Pure server plan: stable definition/level/quantity order; locked items retain their slots. */
class RPGINVENTORYRUNTIME_API FRPGInventorySortPlanner
{
public:
	static FRPGItemCommandPlan Prepare(const IRPGItemRecordSource& Records,
		const FGuid& RequestId, const FRPGItemOwnerRef& Owner, EItemCategory Category);
};
