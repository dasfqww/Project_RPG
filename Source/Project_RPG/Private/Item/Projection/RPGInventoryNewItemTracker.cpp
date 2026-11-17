#include "Item/Projection/RPGInventoryNewItemTracker.h"

void FRPGInventoryNewItemTracker::Reset()
{
	bHasBaseline = false;
	Previous.Reset();
	UnseenItems.Reset();
}

void FRPGInventoryNewItemTracker::Observe(const TArray<FRPGInventoryProjectionEntry>& Entries)
{
	// Count equipment too: unequipping is not an acquisition. Mail is deliberately
	// excluded so claiming an attachment into the bag can attract attention.
	using FDefinitionKey = TTuple<FPrimaryAssetId, int32>;
	TMap<FDefinitionKey, int64> OldTotals, NewTotals;
	for (const auto& Pair : Previous)
	{
		const auto& Entry = Pair.Value;
		OldTotals.FindOrAdd({Entry.GetDefinitionId(), Entry.GetDefinitionVersion()}) += Entry.GetQuantity();
	}
	TMap<FGuid, FRPGInventoryProjectionEntry> Current;
	TSet<FGuid> BagIds;
	for (const auto& Entry : Entries)
	{
		if (Entry.GetContainerType() != ERPGItemContainerType::Inventory &&
			Entry.GetContainerType() != ERPGItemContainerType::Equipment) continue;
		Current.Add(Entry.GetItemId(), Entry);
		NewTotals.FindOrAdd({Entry.GetDefinitionId(), Entry.GetDefinitionVersion()}) += Entry.GetQuantity();
		if (Entry.GetContainerType() == ERPGItemContainerType::Inventory) BagIds.Add(Entry.GetItemId());
	}
	for (auto It = UnseenItems.CreateIterator(); It; ++It)
		if (!BagIds.Contains(*It)) It.RemoveCurrent();
	if (bHasBaseline)
	{
		for (const auto& Pair : Current)
		{
			const auto& Entry = Pair.Value;
			if (!BagIds.Contains(Pair.Key)) continue;
			const FDefinitionKey Key(Entry.GetDefinitionId(), Entry.GetDefinitionVersion());
			const auto* Old = Previous.Find(Pair.Key);
			// A split/merge changes per-ID quantities, but does not increase the
			// owned total. Revisions, sorting and slot changes alone are ignored.
			if (NewTotals.FindRef(Key) > OldTotals.FindRef(Key) &&
				(!Old || Entry.GetQuantity() > Old->GetQuantity()))
				UnseenItems.Add(Pair.Key);
		}
	}
	Previous = MoveTemp(Current);
	bHasBaseline = true;
}
