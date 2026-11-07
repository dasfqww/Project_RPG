#include "Item/Transaction/RPGItemPickupGrantPlanner.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"

namespace
{
bool Fail(FRPGItemRepositoryCommitRequest& OutCommit, FString* OutError,
	const TCHAR* Message)
{
	OutCommit = FRPGItemRepositoryCommitRequest();
	if (OutError)
	{
		*OutError = Message;
	}
	return false;
}
}

bool FRPGItemPickupGrantPlanner::TryPlan(
	const FRPGItemPickupGrantRequest& Request,
	const URPGItemDefinition& Definition,
	const IRPGItemRecordSource& Records,
	const IRPGItemDefinitionCatalog& Definitions,
	FRPGItemRepositoryCommitRequest& OutCommit,
	FString* OutError)
{
	OutCommit = FRPGItemRepositoryCommitRequest();
	if (OutError)
	{
		OutError->Reset();
	}
	if (!Request.RequestId.IsValid() || !Request.ItemId.IsValid() ||
		Request.Owner.Type != ERPGItemOwnerType::Character ||
		!Request.Owner.IsValid() || Request.Quantity <= 0 ||
		Request.CreationSource.IsEmpty() || Request.CreationSource.Len() > 128)
	{
		return Fail(OutCommit, OutError, TEXT("Invalid pickup grant request."));
	}

	const FPrimaryAssetId DefinitionId = Definition.GetPrimaryAssetId();
	FRPGItemDefinitionSnapshot Snapshot;
	if (!Definition.ItemTag.IsValid() ||
		Definition.ItemCategory == EItemCategory::None ||
		!Definitions.TryFindDefinition(DefinitionId, Snapshot) ||
		!Snapshot.IsValid() || Snapshot.DefinitionId != DefinitionId ||
		Snapshot.DefinitionVersion != Definition.GetDefinitionVersion() ||
		Snapshot.MaxStackSize != Definition.GetMaxStackSize())
	{
		return Fail(OutCommit, OutError,
			TEXT("A matching native item definition is unavailable."));
	}
	if (Request.Quantity > Snapshot.MaxStackSize)
	{
		return Fail(OutCommit, OutError,
			TEXT("Pickup quantity exceeds the definition stack limit."));
	}

	FRPGItemRecord ExistingIdentity;
	if (Records.Find(Request.ItemId, ExistingIdentity))
	{
		return Fail(OutCommit, OutError,
			TEXT("The pickup item identity already exists."));
	}

	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = Request.Owner.OwnerId;
	Location.SlotIndex = INDEX_NONE;
	const int32 First = RPGItemInventoryPolicy::FirstSlot(RPGItemInventoryPolicy::CategoryForDefinition(DefinitionId));
	for (int32 Slot = First; Slot < First + RPGItemInventoryPolicy::SlotsPerTab; ++Slot)
	{
		if (Request.ReservedInventorySlots.Contains(Slot)) continue;
		Location.SlotIndex = Slot;
		FRPGItemRecord Occupant;
		if (!Records.FindAtLocation(Request.Owner, Location, Occupant))
		{
			break;
		}
		Location.SlotIndex = INDEX_NONE;
	}
	if (Location.SlotIndex == INDEX_NONE)
	{
		return Fail(OutCommit, OutError, TEXT("The inventory is full."));
	}

	FRPGItemInstanceState GeneratedState;
	Definition.BuildInstanceState(
		Request.Quantity, Request.GenerationSeed, GeneratedState);
	FRPGItemInstanceState StableState;
	if (!FRPGItemInstanceState::TryRestore(
		Request.ItemId, Request.GenerationSeed, Request.Quantity,
		GeneratedState.GetInstanceTags(), GeneratedState.GetStatValues(),
		StableState))
	{
		return Fail(OutCommit, OutError,
			TEXT("The pickup instance state could not be built."));
	}
	FRPGItemRecordMetadata Metadata;
	Metadata.CreationSource = Request.CreationSource;
	FRPGItemRecord NewRecord;
	if (!FRPGItemRecord::TryCreate(
		DefinitionId, Snapshot.DefinitionVersion, Request.Owner,
		Location, StableState, Metadata, NewRecord))
	{
		return Fail(OutCommit, OutError,
			TEXT("The pickup item record could not be built."));
	}

	OutCommit.RequestId = Request.RequestId;
	OutCommit.Operation = TEXT("Item.Pickup");
	OutCommit.CommandFingerprint = FString::Printf(
		TEXT("%s|%s|%d|%d|%d|%s|%d|%s"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*DefinitionId.ToString(), Snapshot.DefinitionVersion,
		Request.Quantity, Request.GenerationSeed, *Request.Owner.OwnerId,
		Location.SlotIndex,
		*Request.CreationSource);
	if (OutCommit.CommandFingerprint.Len() > 512)
	{
		return Fail(OutCommit, OutError,
			TEXT("The pickup command fingerprint is too long."));
	}
	OutCommit.Actor = Request.Owner;
	OutCommit.AffectedQuantity = Request.Quantity;
	FRPGItemRecordMutation& Mutation =
		OutCommit.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = 0;
	Mutation.NewRecord = MoveTemp(NewRecord);
	return true;
}
