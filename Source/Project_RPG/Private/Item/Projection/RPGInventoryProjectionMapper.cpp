#include "Item/Projection/RPGInventoryProjectionMapper.h"

namespace RPGInventoryProjectionMapper
{
void SetError(FString* OutError, const TCHAR* Message)
{
	if (OutError)
	{
		*OutError = Message;
	}
}

bool GuidLess(const FGuid& Left, const FGuid& Right)
{
	if (Left.A != Right.A) return Left.A < Right.A;
	if (Left.B != Right.B) return Left.B < Right.B;
	if (Left.C != Right.C) return Left.C < Right.C;
	return Left.D < Right.D;
}
}

bool FRPGInventoryProjectionMapper::BuildInventorySnapshot(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	OutEntries.Reset();
	if (!ExpectedOwner.IsValid() ||
		ExpectedOwner.Type != ERPGItemOwnerType::Character)
	{
		RPGInventoryProjectionMapper::SetError(
			OutError,
			TEXT("The inventory projection owner must be a character."));
		return false;
	}

	TSet<FGuid> ItemIds;
	TSet<int32> SlotIndices;
	for (const FRPGItemRecord& Record : Records)
	{
		if (!Record.IsStructurallyValid())
		{
			RPGInventoryProjectionMapper::SetError(
				OutError,
				TEXT("The backend returned an invalid item record."));
			OutEntries.Reset();
			return false;
		}
		if (Record.GetOwner() != ExpectedOwner)
		{
			RPGInventoryProjectionMapper::SetError(
				OutError,
				TEXT("The backend returned an item for another owner."));
			OutEntries.Reset();
			return false;
		}
		if (!Record.IsActive() ||
			Record.GetLocation().ContainerType !=
				ERPGItemContainerType::Inventory)
		{
			continue;
		}

		const FGuid& ItemId = Record.GetItemId();
		const int32 SlotIndex = Record.GetLocation().SlotIndex;
		if (ItemIds.Contains(ItemId) || SlotIndices.Contains(SlotIndex))
		{
			RPGInventoryProjectionMapper::SetError(
				OutError,
				TEXT("The inventory snapshot contains duplicate identity or slot data."));
			OutEntries.Reset();
			return false;
		}
		ItemIds.Add(ItemId);
		SlotIndices.Add(SlotIndex);

		FRPGInventoryProjectionEntry& Entry =
			OutEntries.AddDefaulted_GetRef();
		Entry.ItemId = ItemId;
		Entry.DefinitionId = Record.GetDefinitionId();
		Entry.DefinitionVersion = Record.GetDefinitionVersion();
		Entry.SlotIndex = SlotIndex;
		Entry.Quantity = Record.GetQuantity();
		Entry.Revision = Record.GetRevision();
		Entry.BindState = Record.GetMetadata().BindState;
		Entry.Durability = Record.GetMetadata().Durability;
		Entry.ExpiresAtUtc = Record.GetMetadata().ExpiresAtUtc;
		Entry.bLocked = Record.GetMetadata().bLocked;
		Entry.InstanceTags = Record.GetState().GetInstanceTags();
		Entry.RolledStats = Record.GetState().GetStatValues();
	}

	OutEntries.Sort(
		[](const FRPGInventoryProjectionEntry& Left,
			const FRPGInventoryProjectionEntry& Right)
		{
			return Left.GetSlotIndex() == Right.GetSlotIndex()
				? RPGInventoryProjectionMapper::GuidLess(
					Left.GetItemId(),
					Right.GetItemId())
				: Left.GetSlotIndex() < Right.GetSlotIndex();
		});
	return true;
}
