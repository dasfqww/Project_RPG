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

uint64 MakeSlotKey(
	const ERPGItemContainerType ContainerType,
	const int32 SlotIndex)
{
	return (static_cast<uint64>(static_cast<uint8>(ContainerType)) << 32)
		| static_cast<uint32>(SlotIndex);
}
}

bool FRPGInventoryProjectionMapper::BuildInventorySnapshot(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	return BuildSnapshot(ExpectedOwner, Records, false, OutEntries, OutError);
}

bool FRPGInventoryProjectionMapper::BuildMailSnapshot(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	return BuildSnapshot(ExpectedOwner, Records, true, OutEntries, OutError);
}

bool FRPGInventoryProjectionMapper::BuildSnapshot(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	const bool bMail,
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
	TSet<uint64> SlotKeys;
	TSet<FString> MailSlotKeys;
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
		const ERPGItemContainerType ContainerType =
			Record.GetLocation().ContainerType;
		const bool bSelectedContainer = bMail
			? ContainerType == ERPGItemContainerType::Mail
			: (ContainerType == ERPGItemContainerType::Inventory ||
				ContainerType == ERPGItemContainerType::Equipment);
		if (!Record.IsActive() || !bSelectedContainer)
		{
			continue;
		}

		const FGuid& ItemId = Record.GetItemId();
		const int32 SlotIndex = Record.GetLocation().SlotIndex;
		const uint64 SlotKey = RPGInventoryProjectionMapper::MakeSlotKey(
			ContainerType,
			SlotIndex);
		// Different dungeon reward containers legitimately reuse attachment slots.
		const FString MailSlotKey = FString::Printf(TEXT("%d:%s:%d"),
			Record.GetLocation().ContainerId.Len(),
			*Record.GetLocation().ContainerId, SlotIndex);
		const bool bDuplicateSlot = bMail
			? MailSlotKeys.Contains(MailSlotKey) : SlotKeys.Contains(SlotKey);
		if (ItemIds.Contains(ItemId) || bDuplicateSlot)
		{
			RPGInventoryProjectionMapper::SetError(
				OutError,
				TEXT("The item projection contains duplicate identity or slot data."));
			OutEntries.Reset();
			return false;
		}
		ItemIds.Add(ItemId);
		SlotKeys.Add(SlotKey);
		MailSlotKeys.Add(MailSlotKey);

		FRPGInventoryProjectionEntry& Entry =
			OutEntries.AddDefaulted_GetRef();
		Entry.ItemId = ItemId;
		Entry.DefinitionId = Record.GetDefinitionId();
		Entry.DefinitionVersion = Record.GetDefinitionVersion();
		Entry.ContainerType = ContainerType;
		Entry.SlotIndex = SlotIndex;
		Entry.Quantity = Record.GetQuantity();
		Entry.Revision = Record.GetRevision();
		Entry.BindState = Record.GetMetadata().BindState;
		Entry.Durability = Record.GetMetadata().Durability;
		Entry.ExpiresAtUtc = Record.GetMetadata().ExpiresAtUtc;
		Entry.bLocked = Record.GetMetadata().bLocked;
		Entry.InstanceTags = Record.GetState().GetInstanceTags();
		Entry.RolledStats = Record.GetState().GetStatValues();
		Entry.EquipmentProgression = Record.GetState().GetEquipmentProgression();
	}

	OutEntries.Sort(
		[](const FRPGInventoryProjectionEntry& Left,
			const FRPGInventoryProjectionEntry& Right)
		{
			if (Left.GetContainerType() != Right.GetContainerType())
			{
				return static_cast<uint8>(Left.GetContainerType()) <
					static_cast<uint8>(Right.GetContainerType());
			}
			return Left.GetSlotIndex() == Right.GetSlotIndex()
				? RPGInventoryProjectionMapper::GuidLess(
					Left.GetItemId(),
					Right.GetItemId())
				: Left.GetSlotIndex() < Right.GetSlotIndex();
		});
	return true;
}
