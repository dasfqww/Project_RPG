#include "Item/Transaction/RPGItemActionCommandPlanner.h"

#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicyTypes.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Policy/RPGItemStackPolicy.h"
#include "Item/Transaction/RPGInventorySortPlanner.h"

namespace RPGItemActionCommandPlanner
{
const FName MoveOperation(TEXT("Item.Move"));
const FName SwapOperation(TEXT("Item.Swap"));
const FName StackTransferOperation(TEXT("Item.StackTransfer"));
const FName SplitOperation(TEXT("Item.Split"));
const FName EquipOperation(TEXT("Item.Equip"));
const FName UnequipOperation(TEXT("Item.Unequip"));
const FName ConsumeOperation(TEXT("Item.Consume"));
const FName ClaimMailOperation(TEXT("Item.ClaimMail"));
}

FRPGItemActionCommandPlanner::FRPGItemActionCommandPlanner(
	const IRPGItemRecordSource& InRecords,
	const IRPGItemDefinitionCatalog& InDefinitions,
	const IRPGItemActionPolicyCatalog& InActions)
	: Records(InRecords)
	, Definitions(InDefinitions)
	, Actions(InActions)
	, Clock(&SystemClock)
{
}

FRPGItemActionCommandPlanner::FRPGItemActionCommandPlanner(
	const IRPGItemRecordSource& InRecords,
	const IRPGItemDefinitionCatalog& InDefinitions,
	const IRPGItemActionPolicyCatalog& InActions,
	const IRPGItemClock& InClock)
	: Records(InRecords)
	, Definitions(InDefinitions)
	, Actions(InActions)
	, Clock(&InClock)
{
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemMoveRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() || !Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0 ||
		Request.Destination.ContainerType != ERPGItemContainerType::Inventory ||
		!Request.Destination.IsActiveLocation() ||
		Request.Destination.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity)
	{
		return false;
	}

	OutDescriptor.Operation = RPGItemActionCommandPlanner::MoveOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(
		TEXT("%s|%lld|%d|%s|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision,
		static_cast<int32>(Request.Destination.ContainerType),
		*Request.Destination.ContainerId,
		Request.Destination.SlotIndex);
	return true;
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemSwapRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character || !Request.Actor.IsValid() ||
		!Request.SourceItemId.IsValid() || !Request.DestinationItemId.IsValid() ||
		Request.SourceItemId == Request.DestinationItemId ||
		Request.ExpectedSourceRevision <= 0 || Request.ExpectedDestinationRevision <= 0 ||
		Request.InventoryContainerId.IsEmpty())
	{
		return false;
	}
	OutDescriptor.Operation = RPGItemActionCommandPlanner::SwapOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(TEXT("%s|%lld|%s|%lld|%s"),
		*Request.SourceItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedSourceRevision,
		*Request.DestinationItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedDestinationRevision, *Request.InventoryContainerId);
	return true;
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemStackTransferRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() || Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() || !Request.SourceItemId.IsValid() ||
		!Request.DestinationItemId.IsValid() || Request.SourceItemId == Request.DestinationItemId ||
		Request.ExpectedSourceRevision <= 0 || Request.ExpectedDestinationRevision <= 0 ||
		Request.RequestedQuantity <= 0)
	{
		return false;
	}
	OutDescriptor.Operation = RPGItemActionCommandPlanner::StackTransferOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(TEXT("%s|%lld|%s|%lld|%d"),
		*Request.SourceItemId.ToString(EGuidFormats::DigitsWithHyphens), Request.ExpectedSourceRevision,
		*Request.DestinationItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedDestinationRevision, Request.RequestedQuantity);
	return true;
}

FGuid FRPGItemActionCommandPlanner::MakeSplitItemId(const FGuid& RequestId, const FGuid& SourceItemId)
{
	if (!RequestId.IsValid() || !SourceItemId.IsValid()) return FGuid();
	const FString Namespace = FString::Printf(TEXT("ProjectRPG.Item.Split.v1|%s|%s"),
		*SourceItemId.ToString(EGuidFormats::Digits), *RequestId.ToString(EGuidFormats::Digits));
	return FGuid::NewDeterministicGuid(Namespace);
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemSplitRequest& Request, FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() || Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() || !Request.ItemId.IsValid() || Request.ExpectedRevision <= 0 || Request.Quantity <= 0 ||
		Request.Destination.ContainerType != ERPGItemContainerType::Inventory ||
		!Request.Destination.IsActiveLocation() || Request.Destination.ContainerId != Request.Actor.OwnerId ||
		Request.Destination.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity) return false;
	OutDescriptor.Operation = RPGItemActionCommandPlanner::SplitOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(TEXT("%s|%lld|%s|%d|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens), Request.ExpectedRevision,
		*Request.Destination.ContainerId, Request.Destination.SlotIndex, Request.Quantity);
	return true;
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemEquipRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() ||
		!Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0 ||
		Request.EquipmentContainerId.IsEmpty() ||
		Request.SlotType == EEquipmentSlotType::None ||
		Request.SlotType == EEquipmentSlotType::Count)
	{
		return false;
	}

	OutDescriptor.Operation =
		RPGItemActionCommandPlanner::EquipOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(
		TEXT("%s|%lld|%s|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision,
		*Request.EquipmentContainerId,
		static_cast<int32>(Request.SlotType));
	return true;
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemUnequipRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() ||
		!Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0 ||
		Request.InventoryDestination.ContainerType !=
			ERPGItemContainerType::Inventory ||
		!Request.InventoryDestination.IsActiveLocation())
	{
		return false;
	}

	OutDescriptor.Operation =
		RPGItemActionCommandPlanner::UnequipOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(
		TEXT("%s|%lld|%s|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision,
		*Request.InventoryDestination.ContainerId,
		Request.InventoryDestination.SlotIndex);
	return true;
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemConsumeRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() ||
		!Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0)
	{
		return false;
	}

	OutDescriptor.Operation =
		RPGItemActionCommandPlanner::ConsumeOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(
		TEXT("%s|%lld"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision);
	return true;
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemMoveRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemRecord StoredRecord;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(Request.ItemId, Request.Actor,
		Request.ExpectedRevision, StoredRecord, FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (StoredRecord.GetLocation().ContainerType != ERPGItemContainerType::Inventory ||
		StoredRecord.GetLocation().ContainerId != Request.Destination.ContainerId)
	{
		return Failure(ERPGItemTransactionStatus::InvalidContainerTransition);
	}
	if (StoredRecord.GetLocation() == Request.Destination)
	{
		return Failure(ERPGItemTransactionStatus::NoChange);
	}

	FRPGItemRecord OccupyingRecord;
	if (Records.FindAtLocation(Request.Actor, Request.Destination, OccupyingRecord))
	{
		return Failure(ERPGItemTransactionStatus::LocationOccupied);
	}

	FRPGItemRecord MovedRecord;
	if (!RPGItemInventoryPolicy::CanStore(StoredRecord.GetDefinitionId(), Request.Destination.SlotIndex) ||
		!StoredRecord.TryMoveTo(Request.Destination, MovedRecord))
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = Descriptor.Operation;
	CommitRequest.CommandFingerprint = Descriptor.CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = StoredRecord.GetQuantity();
	FRPGItemRecordMutation& Mutation = CommitRequest.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(MovedRecord);
	return Ready(MoveTemp(CommitRequest));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemSwapRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemRecord Source;
	FRPGItemRecord Destination;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(Request.SourceItemId, Request.Actor,
		Request.ExpectedSourceRevision, Source, FailureStatus) ||
		!TryLoadMutableRecord(Request.DestinationItemId, Request.Actor,
			Request.ExpectedDestinationRevision, Destination, FailureStatus))
	{
		return Failure(FailureStatus);
	}
	const auto IsBagLocation = [&Request](const FRPGItemLocation& Location)
	{
		return Location.ContainerType == ERPGItemContainerType::Inventory &&
			Location.ContainerId == Request.InventoryContainerId &&
			Location.SlotIndex >= 0 && Location.SlotIndex < RPGItemInventoryPolicy::SlotCapacity;
	};
	if (!IsBagLocation(Source.GetLocation()) || !IsBagLocation(Destination.GetLocation()))
	{
		return Failure(ERPGItemTransactionStatus::InvalidContainerTransition);
	}
	if (Source.GetLocation() == Destination.GetLocation())
	{
		return Failure(ERPGItemTransactionStatus::NoChange);
	}

	FRPGItemRecord MovedSource;
	FRPGItemRecord MovedDestination;
	if (!RPGItemInventoryPolicy::CanStore(Source.GetDefinitionId(), Destination.GetLocation().SlotIndex) ||
		!RPGItemInventoryPolicy::CanStore(Destination.GetDefinitionId(), Source.GetLocation().SlotIndex) ||
		!Source.TryMoveTo(Destination.GetLocation(), MovedSource) ||
		!Destination.TryMoveTo(Source.GetLocation(), MovedDestination))
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}

	// Both records and their revisions belong to one CAS transaction. There is
	// no intermediate empty slot, second request, or local rollback to perform.
	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = Descriptor.Operation;
	CommitRequest.CommandFingerprint = Descriptor.CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	FRPGItemRecordMutation& SourceMutation = CommitRequest.Mutations.AddDefaulted_GetRef();
	SourceMutation.ExpectedRevision = Request.ExpectedSourceRevision;
	SourceMutation.NewRecord = MoveTemp(MovedSource);
	FRPGItemRecordMutation& DestinationMutation = CommitRequest.Mutations.AddDefaulted_GetRef();
	DestinationMutation.ExpectedRevision = Request.ExpectedDestinationRevision;
	DestinationMutation.NewRecord = MoveTemp(MovedDestination);
	return Ready(MoveTemp(CommitRequest));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemStackTransferRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor)) return Failure(ERPGItemTransactionStatus::InvalidRequest);
	FRPGItemRecord Source;
	FRPGItemRecord Destination;
	ERPGItemTransactionStatus Status;
	if (!TryLoadMutableRecord(Request.SourceItemId, Request.Actor, Request.ExpectedSourceRevision, Source, Status) ||
		!TryLoadMutableRecord(Request.DestinationItemId, Request.Actor,
			Request.ExpectedDestinationRevision, Destination, Status)) return Failure(Status);
	const auto IsBagLocation = [&Request](const FRPGItemLocation& Location)
	{
		return Location.ContainerType == ERPGItemContainerType::Inventory &&
			Location.ContainerId == Request.Actor.OwnerId &&
			Location.SlotIndex >= 0 && Location.SlotIndex < RPGItemInventoryPolicy::SlotCapacity;
	};
	if (!IsBagLocation(Source.GetLocation()) || !IsBagLocation(Destination.GetLocation()) ||
		Source.GetLocation() == Destination.GetLocation())
		return Failure(ERPGItemTransactionStatus::InvalidContainerTransition);
	if (Source.GetDefinitionId() != Destination.GetDefinitionId() ||
		Source.GetDefinitionVersion() != Destination.GetDefinitionVersion() ||
		!FRPGItemStackPolicy::AreInstanceStatesCompatible(Source.GetState(), Destination.GetState()) ||
		!FRPGItemStackPolicy::AreMetadataCompatible(Source.GetMetadata(), Destination.GetMetadata()))
		return Failure(ERPGItemTransactionStatus::IncompatibleStack);
	FRPGItemDefinitionSnapshot Definition;
	if (!Definitions.TryFindDefinition(Source.GetDefinitionId(), Definition) || !Definition.IsValid() ||
		Definition.DefinitionId != Source.GetDefinitionId())
		return Failure(ERPGItemTransactionStatus::DefinitionUnavailable);
	if (Definition.DefinitionVersion != Source.GetDefinitionVersion())
		return Failure(ERPGItemTransactionStatus::DefinitionVersionMismatch);
	if (Definition.MaxStackSize <= 1 || Source.GetQuantity() > Definition.MaxStackSize ||
		Destination.GetQuantity() > Definition.MaxStackSize)
		return Failure(ERPGItemTransactionStatus::IncompatibleStack);
	const int32 Transferred = FMath::Min3(Request.RequestedQuantity, Source.GetQuantity(),
		Definition.MaxStackSize - Destination.GetQuantity());
	if (Transferred <= 0) return Failure(ERPGItemTransactionStatus::StackFull);
	const int32 Remaining = Source.GetQuantity() - Transferred;
	FRPGItemRecord NewSource;
	FRPGItemRecord NewDestination;
	if (!(Remaining > 0 ? Source.TrySetQuantity(Remaining, NewSource) : Source.TryMarkConsumed(NewSource)) ||
		!Destination.TrySetQuantity(Destination.GetQuantity() + Transferred, NewDestination))
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);

	FRPGItemRepositoryCommitRequest Commit;
	Commit.RequestId = Request.RequestId;
	Commit.Operation = Descriptor.Operation;
	Commit.CommandFingerprint = Descriptor.CommandFingerprint;
	Commit.Actor = Request.Actor;
	Commit.AffectedQuantity = Transferred;
	FRPGItemRecordMutation& SourceMutation = Commit.Mutations.AddDefaulted_GetRef();
	SourceMutation.ExpectedRevision = Request.ExpectedSourceRevision;
	SourceMutation.NewRecord = MoveTemp(NewSource);
	FRPGItemRecordMutation& DestinationMutation = Commit.Mutations.AddDefaulted_GetRef();
	DestinationMutation.ExpectedRevision = Request.ExpectedDestinationRevision;
	DestinationMutation.NewRecord = MoveTemp(NewDestination);
	return Ready(MoveTemp(Commit));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(const FRPGItemSplitRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor)) return Failure(ERPGItemTransactionStatus::InvalidRequest);
	FRPGItemRecord Source;
	ERPGItemTransactionStatus Status;
	if (!TryLoadMutableRecord(Request.ItemId, Request.Actor, Request.ExpectedRevision, Source, Status)) return Failure(Status);
	const auto& Location = Source.GetLocation();
	if (Location.ContainerType != ERPGItemContainerType::Inventory || Location.ContainerId != Request.Actor.OwnerId ||
		Location.SlotIndex < 0 || Location.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity || Location == Request.Destination)
		return Failure(ERPGItemTransactionStatus::InvalidContainerTransition);
	if (Request.Quantity >= Source.GetQuantity()) return Failure(ERPGItemTransactionStatus::InsufficientQuantity);
	FRPGItemDefinitionSnapshot Definition;
	if (!Definitions.TryFindDefinition(Source.GetDefinitionId(), Definition) || !Definition.IsValid() ||
		Definition.DefinitionId != Source.GetDefinitionId()) return Failure(ERPGItemTransactionStatus::DefinitionUnavailable);
	if (Definition.DefinitionVersion != Source.GetDefinitionVersion()) return Failure(ERPGItemTransactionStatus::DefinitionVersionMismatch);
	if (Definition.MaxStackSize <= 1 || Source.GetQuantity() > Definition.MaxStackSize)
		return Failure(ERPGItemTransactionStatus::IncompatibleStack);
	FRPGItemRecord Collision;
	if (Records.FindAtLocation(Request.Actor, Request.Destination, Collision))
		return Failure(ERPGItemTransactionStatus::LocationOccupied);
	const FGuid NewId = MakeSplitItemId(Request.RequestId, Request.ItemId);
	if (!NewId.IsValid() || NewId == Request.ItemId || Records.Find(NewId, Collision))
		return Failure(ERPGItemTransactionStatus::RepositoryRejected); // never pick a different ID on retry/collision
	FRPGItemRecord Remaining;
	FRPGItemRecord Created;
	FRPGItemInstanceState State;
	if (!Source.TrySetQuantity(Source.GetQuantity() - Request.Quantity, Remaining) ||
		!FRPGItemInstanceState::TryRestore(NewId, Source.GetState().GetGenerationSeed(), Request.Quantity,
			Source.GetState().GetInstanceTags(), Source.GetState().GetStatValues(), State,
			Source.GetState().GetEquipmentProgression()) ||
		!RPGItemInventoryPolicy::CanStore(Source.GetDefinitionId(), Request.Destination.SlotIndex) ||
		!FRPGItemRecord::TryCreate(Source.GetDefinitionId(), Source.GetDefinitionVersion(), Request.Actor,
			Request.Destination, State, Source.GetMetadata(), Created)) return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	FRPGItemRepositoryCommitRequest Commit;
	Commit.RequestId = Request.RequestId;
	Commit.Operation = Descriptor.Operation;
	Commit.CommandFingerprint = Descriptor.CommandFingerprint;
	Commit.Actor = Request.Actor;
	Commit.AffectedQuantity = Request.Quantity;
	FRPGItemRecordMutation& SourceMutation = Commit.Mutations.AddDefaulted_GetRef();
	SourceMutation.ExpectedRevision = Request.ExpectedRevision;
	SourceMutation.NewRecord = MoveTemp(Remaining);
	FRPGItemRecordMutation& CreatedMutation = Commit.Mutations.AddDefaulted_GetRef();
	CreatedMutation.ExpectedRevision = 0;
	CreatedMutation.NewRecord = MoveTemp(Created);
	return Ready(MoveTemp(Commit));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemEquipRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemRecord StoredRecord;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(
		Request.ItemId,
		Request.Actor,
		Request.ExpectedRevision,
		StoredRecord,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (StoredRecord.GetLocation().ContainerType !=
		ERPGItemContainerType::Inventory)
	{
		return Failure(
			ERPGItemTransactionStatus::InvalidContainerTransition);
	}

	FRPGItemDefinitionSnapshot Definition;
	FRPGItemActionPolicy Policy;
	if (!TryResolveActionPolicy(
		StoredRecord,
		Definition,
		Policy,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (!Policy.IsEquipment() ||
		Definition.MaxStackSize != 1 ||
		StoredRecord.GetQuantity() != 1)
	{
		return Failure(ERPGItemTransactionStatus::NotEquippable);
	}
	if (!Policy.CanEquipInSlot(Request.SlotType))
	{
		return Failure(
			ERPGItemTransactionStatus::IncompatibleEquipmentSlot);
	}

	FRPGItemLocation EquipmentLocation;
	EquipmentLocation.ContainerType = ERPGItemContainerType::Equipment;
	EquipmentLocation.ContainerId = Request.EquipmentContainerId;
	EquipmentLocation.SlotIndex = static_cast<int32>(Request.SlotType);
	FRPGItemRecord OccupyingRecord;
	if (Records.FindAtLocation(
		Request.Actor,
		EquipmentLocation,
		OccupyingRecord))
	{
		return Failure(ERPGItemTransactionStatus::LocationOccupied);
	}

	FRPGItemRecord EquippedRecord;
	if (!StoredRecord.TryEquipTo(EquipmentLocation, EquippedRecord))
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = Descriptor.Operation;
	CommitRequest.CommandFingerprint = Descriptor.CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = 1;
	FRPGItemRecordMutation& Mutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(EquippedRecord);
	return Ready(MoveTemp(CommitRequest));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemUnequipRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemRecord StoredRecord;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(
		Request.ItemId,
		Request.Actor,
		Request.ExpectedRevision,
		StoredRecord,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (StoredRecord.GetLocation().ContainerType !=
		ERPGItemContainerType::Equipment)
	{
		return Failure(
			ERPGItemTransactionStatus::InvalidContainerTransition);
	}

	FRPGItemDefinitionSnapshot Definition;
	FRPGItemActionPolicy Policy;
	if (!TryResolveActionPolicy(
		StoredRecord,
		Definition,
		Policy,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (!Policy.IsEquipment() ||
		Definition.MaxStackSize != 1 ||
		StoredRecord.GetQuantity() != 1)
	{
		return Failure(ERPGItemTransactionStatus::NotEquippable);
	}

	const int32 StoredSlot = StoredRecord.GetLocation().SlotIndex;
	if (StoredSlot < 0 ||
		StoredSlot >= static_cast<int32>(EEquipmentSlotType::Count) ||
		!Policy.CanEquipInSlot(
			static_cast<EEquipmentSlotType>(StoredSlot)))
	{
		return Failure(
			ERPGItemTransactionStatus::IncompatibleEquipmentSlot);
	}

	FRPGItemRecord OccupyingRecord;
	if (Records.FindAtLocation(
		Request.Actor,
		Request.InventoryDestination,
		OccupyingRecord))
	{
		return Failure(ERPGItemTransactionStatus::LocationOccupied);
	}

	FRPGItemRecord UnequippedRecord;
	if (!RPGItemInventoryPolicy::CanStore(StoredRecord.GetDefinitionId(), Request.InventoryDestination.SlotIndex) || !StoredRecord.TryMoveTo(
		Request.InventoryDestination,
		UnequippedRecord))
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = Descriptor.Operation;
	CommitRequest.CommandFingerprint = Descriptor.CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = 1;
	FRPGItemRecordMutation& Mutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(UnequippedRecord);
	return Ready(MoveTemp(CommitRequest));
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemConsumeRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemRecord StoredRecord;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(
		Request.ItemId,
		Request.Actor,
		Request.ExpectedRevision,
		StoredRecord,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (StoredRecord.GetLocation().ContainerType !=
		ERPGItemContainerType::Inventory)
	{
		return Failure(
			ERPGItemTransactionStatus::InvalidContainerTransition);
	}

	FRPGItemDefinitionSnapshot Definition;
	FRPGItemActionPolicy Policy;
	if (!TryResolveActionPolicy(
		StoredRecord,
		Definition,
		Policy,
		FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (!Policy.IsConsumable())
	{
		return Failure(ERPGItemTransactionStatus::NotConsumable);
	}
	if (StoredRecord.GetQuantity() < Policy.QuantityPerUse)
	{
		return Failure(ERPGItemTransactionStatus::InsufficientQuantity);
	}

	const int32 RemainingQuantity =
		StoredRecord.GetQuantity() - Policy.QuantityPerUse;
	FRPGItemRecord ConsumedRecord;
	const bool bChanged = RemainingQuantity > 0
		? StoredRecord.TrySetQuantity(RemainingQuantity, ConsumedRecord)
		: StoredRecord.TryMarkConsumed(ConsumedRecord);
	if (!bChanged)
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = Descriptor.Operation;
	CommitRequest.CommandFingerprint = Descriptor.CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = Policy.QuantityPerUse;
	FRPGItemRecordMutation& Mutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(ConsumedRecord);
	return Ready(MoveTemp(CommitRequest));
}

bool FRPGItemActionCommandPlanner::TryDescribe(
	const FRPGItemClaimMailRequest& Request,
	FRPGItemCommandDescriptor& OutDescriptor)
{
	if (!Request.RequestId.IsValid() ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!Request.Actor.IsValid() || !Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0 ||
		Request.InventoryDestination.ContainerType != ERPGItemContainerType::Inventory ||
		Request.InventoryDestination.ContainerId != Request.Actor.OwnerId ||
		!Request.InventoryDestination.IsActiveLocation() ||
		Request.InventoryDestination.SlotIndex >= RPGItemInventoryPolicy::SlotCapacity)
	{
		return false;
	}
	OutDescriptor.Operation = RPGItemActionCommandPlanner::ClaimMailOperation;
	OutDescriptor.CommandFingerprint = FString::Printf(
		TEXT("%s|%lld|%s|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision, *Request.InventoryDestination.ContainerId,
		Request.InventoryDestination.SlotIndex);
	return true;
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Prepare(
	const FRPGItemClaimMailRequest& Request) const
{
	FRPGItemCommandDescriptor Descriptor;
	if (!TryDescribe(Request, Descriptor))
	{
		return Failure(ERPGItemTransactionStatus::InvalidRequest);
	}
	FRPGItemRecord StoredRecord;
	ERPGItemTransactionStatus FailureStatus;
	if (!TryLoadMutableRecord(Request.ItemId, Request.Actor,
		Request.ExpectedRevision, StoredRecord, FailureStatus))
	{
		return Failure(FailureStatus);
	}
	if (StoredRecord.GetLocation().ContainerType != ERPGItemContainerType::Mail)
	{
		return Failure(ERPGItemTransactionStatus::InvalidContainerTransition);
	}
	// Claiming does not require an equip/consume policy (e.g. crafting materials).
	FRPGItemDefinitionSnapshot Definition;
	if (!Definitions.TryFindDefinition(StoredRecord.GetDefinitionId(), Definition) ||
		!Definition.IsValid())
	{
		return Failure(ERPGItemTransactionStatus::DefinitionUnavailable);
	}
	if (Definition.DefinitionVersion != StoredRecord.GetDefinitionVersion())
	{
		return Failure(ERPGItemTransactionStatus::DefinitionVersionMismatch);
	}
	if (StoredRecord.GetQuantity() > Definition.MaxStackSize)
	{
		return Failure(ERPGItemTransactionStatus::StackFull);
	}
	FRPGItemRecord OccupyingRecord;
	if (Records.FindAtLocation(Request.Actor, Request.InventoryDestination, OccupyingRecord))
	{
		return Failure(ERPGItemTransactionStatus::LocationOccupied);
	}
	FRPGItemRecord ClaimedRecord;
	if (!RPGItemInventoryPolicy::CanStore(StoredRecord.GetDefinitionId(), Request.InventoryDestination.SlotIndex) ||
		!StoredRecord.TryMoveTo(Request.InventoryDestination, ClaimedRecord))
	{
		return Failure(ERPGItemTransactionStatus::RepositoryRejected);
	}
	FRPGItemRepositoryCommitRequest Commit;
	Commit.RequestId = Request.RequestId;
	Commit.Operation = Descriptor.Operation;
	Commit.CommandFingerprint = Descriptor.CommandFingerprint;
	Commit.Actor = Request.Actor;
	Commit.AffectedQuantity = StoredRecord.GetQuantity();
	FRPGItemRecordMutation& Mutation = Commit.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(ClaimedRecord);
	return Ready(MoveTemp(Commit));
}

bool FRPGItemActionCommandPlanner::TryLoadMutableRecord(
	const FGuid& ItemId,
	const FRPGItemOwnerRef& Actor,
	const int64 ExpectedRevision,
	FRPGItemRecord& OutRecord,
	ERPGItemTransactionStatus& OutFailureStatus) const
{
	if (!Records.Find(ItemId, OutRecord))
	{
		OutFailureStatus = ERPGItemTransactionStatus::NotFound;
		return false;
	}
	if (OutRecord.GetOwner() != Actor)
	{
		OutFailureStatus = ERPGItemTransactionStatus::Forbidden;
		return false;
	}
	if (!OutRecord.IsActive())
	{
		OutFailureStatus = ERPGItemTransactionStatus::NotActive;
		return false;
	}
	if (OutRecord.GetMetadata().bLocked)
	{
		OutFailureStatus = ERPGItemTransactionStatus::Locked;
		return false;
	}
	if (OutRecord.IsExpiredAt(Clock->UtcNow()))
	{
		OutFailureStatus = ERPGItemTransactionStatus::Expired;
		return false;
	}
	if (OutRecord.GetRevision() != ExpectedRevision)
	{
		OutFailureStatus = ERPGItemTransactionStatus::RevisionConflict;
		return false;
	}
	return true;
}

bool FRPGItemActionCommandPlanner::TryResolveActionPolicy(
	const FRPGItemRecord& Record,
	FRPGItemDefinitionSnapshot& OutDefinition,
	FRPGItemActionPolicy& OutPolicy,
	ERPGItemTransactionStatus& OutFailureStatus) const
{
	if (!Definitions.TryFindDefinition(
			Record.GetDefinitionId(),
			OutDefinition) ||
		!Actions.TryFindActionPolicy(
			Record.GetDefinitionId(),
			OutPolicy))
	{
		OutFailureStatus =
			ERPGItemTransactionStatus::DefinitionUnavailable;
		return false;
	}
	if (OutDefinition.DefinitionVersion != Record.GetDefinitionVersion() ||
		OutPolicy.DefinitionVersion != Record.GetDefinitionVersion())
	{
		OutFailureStatus =
			ERPGItemTransactionStatus::DefinitionVersionMismatch;
		return false;
	}
	return true;
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Failure(
	const ERPGItemTransactionStatus Status)
{
	FRPGItemCommandPlan Result;
	Result.FailureStatus = Status;
	return Result;
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::Ready(
	FRPGItemRepositoryCommitRequest&& CommitRequest)
{
	FRPGItemCommandPlan Result;
	Result.bReady = true;
	Result.CommitRequest = MoveTemp(CommitRequest);
	return Result;
}

FRPGItemCommandPlan FRPGItemActionCommandPlanner::PrepareSort(const FGuid& RequestId,
	const FRPGItemOwnerRef& Owner, EItemCategory Category) const
{ return FRPGInventorySortPlanner::Prepare(Records, RequestId, Owner, Category); }
