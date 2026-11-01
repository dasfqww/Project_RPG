#pragma once

#include "CoreMinimal.h"

struct FRPGInventoryProjectionEntry;

/** Read-only formatting of owner-visible item data; no assets, RPCs or gameplay policy. */
class PROJECT_RPG_API FRPGItemTooltipFormatter final
{
public:
	/** Zero means the matching definition's stack limit is unavailable. */
	static FText BuildInstanceDetails(const FRPGInventoryProjectionEntry& Entry,
		int32 MaximumStackSize = 0);
	static FText JoinDetails(const FText& DisplayName, const FText& Description,
		const FText& InstanceDetails);
};
