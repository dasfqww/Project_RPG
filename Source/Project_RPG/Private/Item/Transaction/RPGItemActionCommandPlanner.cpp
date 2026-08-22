#include "Item/Transaction/RPGItemActionCommandPlanner.h"

#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicy.h"

namespace RPGItemActionCommandPlanner
{
const FName EquipOperation(TEXT("Item.Equip"));
const FName UnequipOperation(TEXT("Item.Unequip"));
const FName ConsumeOperation(TEXT("Item.Consume"));
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
	if (!StoredRecord.TryMoveTo(
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
