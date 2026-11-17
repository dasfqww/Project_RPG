#pragma once

#include "CoreMinimal.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"

/** Session-local attention state. Never changes an item or authorizes a command. */
class PROJECT_RPG_API FRPGInventoryNewItemTracker
{
public:
	void Observe(const TArray<FRPGInventoryProjectionEntry>& Entries);
	void Reset();
	bool IsUnseen(FGuid ItemId) const { return UnseenItems.Contains(ItemId); }
	bool MarkSeen(FGuid ItemId) { return UnseenItems.Remove(ItemId) > 0; }

private:
	bool bHasBaseline = false;
	TMap<FGuid, FRPGInventoryProjectionEntry> Previous;
	TSet<FGuid> UnseenItems;
};
