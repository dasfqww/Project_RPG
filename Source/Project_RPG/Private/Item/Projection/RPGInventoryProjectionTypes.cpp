#include "Item/Projection/RPGInventoryProjectionTypes.h"

#include "Component/RPGInventoryProjectionComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionTypes)

namespace RPGInventoryProjectionTypes
{
bool StatValuesEqual(
	const TArray<FRPGItemStatValue>& Left,
	const TArray<FRPGItemStatValue>& Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		if (Left[Index].StatTag != Right[Index].StatTag ||
			Left[Index].Value != Right[Index].Value)
		{
			return false;
		}
	}
	return true;
}
}

bool FRPGInventoryProjectionEntry::HasSamePayload(
	const FRPGInventoryProjectionEntry& Other) const
{
	return ItemId == Other.ItemId &&
		DefinitionId == Other.DefinitionId &&
		DefinitionVersion == Other.DefinitionVersion &&
		SlotIndex == Other.SlotIndex &&
		Quantity == Other.Quantity &&
		Revision == Other.Revision &&
		BindState == Other.BindState &&
		Durability.Current == Other.Durability.Current &&
		Durability.Maximum == Other.Durability.Maximum &&
		ExpiresAtUtc == Other.ExpiresAtUtc &&
		bLocked == Other.bLocked &&
		InstanceTags == Other.InstanceTags &&
		RPGInventoryProjectionTypes::StatValuesEqual(
			RolledStats,
			Other.RolledStats);
}

void FRPGInventoryProjectionEntry::CopyPayloadFrom(
	const FRPGInventoryProjectionEntry& Other)
{
	ItemId = Other.ItemId;
	DefinitionId = Other.DefinitionId;
	DefinitionVersion = Other.DefinitionVersion;
	SlotIndex = Other.SlotIndex;
	Quantity = Other.Quantity;
	Revision = Other.Revision;
	BindState = Other.BindState;
	Durability = Other.Durability;
	ExpiresAtUtc = Other.ExpiresAtUtc;
	bLocked = Other.bLocked;
	InstanceTags = Other.InstanceTags;
	RolledStats = Other.RolledStats;
}

void FRPGInventoryProjectionList::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	if (OwnerComponent)
	{
		OwnerComponent->HandleProjectionReplicated();
	}
}

bool FRPGInventoryProjectionList::Reconcile(
	const TArray<FRPGInventoryProjectionEntry>& DesiredEntries,
	bool& bOutChanged)
{
	bOutChanged = false;
	TSet<FGuid> DesiredItemIds;
	for (const FRPGInventoryProjectionEntry& Desired : DesiredEntries)
	{
		if (!Desired.GetItemId().IsValid() ||
			DesiredItemIds.Contains(Desired.GetItemId()))
		{
			return false;
		}
		DesiredItemIds.Add(Desired.GetItemId());
	}

	TSet<FGuid> RetainedItemIds;
	bool bRemovedAny = false;
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		const FGuid ItemId = Entries[Index].GetItemId();
		if (!ItemId.IsValid() ||
			!DesiredItemIds.Contains(ItemId) ||
			RetainedItemIds.Contains(ItemId))
		{
			Entries.RemoveAt(Index);
			bRemovedAny = true;
			continue;
		}
		RetainedItemIds.Add(ItemId);
	}
	if (bRemovedAny)
	{
		MarkArrayDirty();
		bOutChanged = true;
	}

	for (const FRPGInventoryProjectionEntry& Desired : DesiredEntries)
	{
		FRPGInventoryProjectionEntry* Existing = Entries.FindByPredicate(
			[&Desired](const FRPGInventoryProjectionEntry& Entry)
			{
				return Entry.GetItemId() == Desired.GetItemId();
			});
		if (!Existing)
		{
			FRPGInventoryProjectionEntry& Added =
				Entries.Add_GetRef(Desired);
			MarkItemDirty(Added);
			bOutChanged = true;
			continue;
		}

		if (!Existing->HasSamePayload(Desired))
		{
			Existing->CopyPayloadFrom(Desired);
			MarkItemDirty(*Existing);
			bOutChanged = true;
		}
	}

	return true;
}

const FRPGInventoryProjectionEntry* FRPGInventoryProjectionList::Find(
	const FGuid& ItemId) const
{
	return Entries.FindByPredicate(
		[&ItemId](const FRPGInventoryProjectionEntry& Entry)
		{
			return Entry.GetItemId() == ItemId;
		});
}
