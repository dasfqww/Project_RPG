#include "Item/Transaction/RPGItemTransactionService.h"

#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Persistence/RPGItemRepository.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Item/Policy/RPGItemStackPolicy.h"

namespace
{
const FName MoveOperation(TEXT("Item.Move"));
const FName StackTransferOperation(TEXT("Item.StackTransfer"));

FString MakeMoveFingerprint(const FRPGItemMoveRequest& Request)
{
	return FString::Printf(
		TEXT("%s|%lld|%d|%s|%d"),
		*Request.ItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedRevision,
		static_cast<int32>(Request.Destination.ContainerType),
		*Request.Destination.ContainerId,
		Request.Destination.SlotIndex);
}

FString MakeStackTransferFingerprint(
	const FRPGItemStackTransferRequest& Request)
{
	return FString::Printf(
		TEXT("%s|%lld|%s|%lld|%d"),
		*Request.SourceItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedSourceRevision,
		*Request.DestinationItemId.ToString(EGuidFormats::DigitsWithHyphens),
		Request.ExpectedDestinationRevision,
		Request.RequestedQuantity);
}

}

FRPGItemTransactionService::FRPGItemTransactionService(
	IRPGItemRepository& InRepository,
	const IRPGItemDefinitionCatalog& InDefinitionCatalog)
	: Repository(InRepository)
	, DefinitionCatalog(InDefinitionCatalog)
	, Clock(&SystemClock)
{
}

FRPGItemTransactionService::FRPGItemTransactionService(
	IRPGItemRepository& InRepository,
	const IRPGItemDefinitionCatalog& InDefinitionCatalog,
	const IRPGItemActionPolicyCatalog& InActionPolicyCatalog)
	: Repository(InRepository)
	, DefinitionCatalog(InDefinitionCatalog)
	, ActionPolicyCatalog(&InActionPolicyCatalog)
	, Clock(&SystemClock)
{
}

FRPGItemTransactionService::FRPGItemTransactionService(
	IRPGItemRepository& InRepository,
	const IRPGItemDefinitionCatalog& InDefinitionCatalog,
	const IRPGItemActionPolicyCatalog& InActionPolicyCatalog,
	const IRPGItemClock& InClock)
	: Repository(InRepository)
	, DefinitionCatalog(InDefinitionCatalog)
	, ActionPolicyCatalog(&InActionPolicyCatalog)
	, Clock(&InClock)
{
}

FRPGItemTransactionService::FRPGItemTransactionService(
	IRPGItemRepository& InRepository,
	const IRPGItemDefinitionCatalog& InDefinitionCatalog,
	const IRPGItemClock& InClock)
	: Repository(InRepository)
	, DefinitionCatalog(InDefinitionCatalog)
	, Clock(&InClock)
{
}

FRPGItemTransactionResult FRPGItemTransactionService::MoveItem(
	const FRPGItemMoveRequest& Request)
{
	if (!Request.RequestId.IsValid() ||
		!Request.Actor.IsValid() ||
		!Request.ItemId.IsValid() ||
		Request.ExpectedRevision <= 0 ||
		!Request.Destination.IsActiveLocation())
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidRequest);
	}

	const FString CommandFingerprint = MakeMoveFingerprint(Request);
	FRPGItemTransactionResult ReplayResult;
	if (TryReplay(
		Request.RequestId,
		Request.Actor,
		MoveOperation,
		CommandFingerprint,
		ReplayResult))
	{
		return ReplayResult;
	}

	FRPGItemRecord StoredRecord;
	if (!Repository.Find(Request.ItemId, StoredRecord))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::NotFound);
	}
	if (StoredRecord.GetOwner() != Request.Actor)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Forbidden);
	}
	if (!StoredRecord.IsActive())
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::NotActive);
	}
	if (StoredRecord.GetMetadata().bLocked)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Locked);
	}
	if (StoredRecord.IsExpiredAt(Clock->UtcNow()))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Expired);
	}
	if (StoredRecord.GetRevision() != Request.ExpectedRevision)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::RevisionConflict);
	}
	if (StoredRecord.GetLocation().ContainerType !=
			ERPGItemContainerType::Inventory ||
		Request.Destination.ContainerType !=
			ERPGItemContainerType::Inventory ||
		StoredRecord.GetLocation().ContainerId !=
			Request.Destination.ContainerId)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidContainerTransition);
	}
	if (StoredRecord.GetLocation() == Request.Destination)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::NoChange);
	}

	FRPGItemRecord OccupyingRecord;
	if (Repository.FindAtLocation(
		Request.Actor,
		Request.Destination,
		OccupyingRecord))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::LocationOccupied);
	}

	FRPGItemRecord MovedRecord;
	if (!StoredRecord.TryMoveTo(Request.Destination, MovedRecord))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = MoveOperation;
	CommitRequest.CommandFingerprint = CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = StoredRecord.GetQuantity();

	FRPGItemRecordMutation& Mutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = Request.ExpectedRevision;
	Mutation.NewRecord = MoveTemp(MovedRecord);

	return FromCommitResult(Repository.Commit(CommitRequest));
}

FRPGItemTransactionResult FRPGItemTransactionService::TransferStack(
	const FRPGItemStackTransferRequest& Request)
{
	if (!Request.RequestId.IsValid() ||
		!Request.Actor.IsValid() ||
		!Request.SourceItemId.IsValid() ||
		!Request.DestinationItemId.IsValid() ||
		Request.SourceItemId == Request.DestinationItemId ||
		Request.ExpectedSourceRevision <= 0 ||
		Request.ExpectedDestinationRevision <= 0 ||
		Request.RequestedQuantity <= 0)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidRequest);
	}

	const FString CommandFingerprint =
		MakeStackTransferFingerprint(Request);
	FRPGItemTransactionResult ReplayResult;
	if (TryReplay(
		Request.RequestId,
		Request.Actor,
		StackTransferOperation,
		CommandFingerprint,
		ReplayResult))
	{
		return ReplayResult;
	}

	FRPGItemRecord SourceRecord;
	FRPGItemRecord DestinationRecord;
	if (!Repository.Find(Request.SourceItemId, SourceRecord) ||
		!Repository.Find(Request.DestinationItemId, DestinationRecord))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::NotFound);
	}
	if (SourceRecord.GetOwner() != Request.Actor ||
		DestinationRecord.GetOwner() != Request.Actor)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Forbidden);
	}
	if (!SourceRecord.IsActive() || !DestinationRecord.IsActive())
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::NotActive);
	}
	if (SourceRecord.GetMetadata().bLocked ||
		DestinationRecord.GetMetadata().bLocked)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Locked);
	}

	const FDateTime UtcNow = Clock->UtcNow();
	if (SourceRecord.IsExpiredAt(UtcNow) ||
		DestinationRecord.IsExpiredAt(UtcNow))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::Expired);
	}
	if (SourceRecord.GetRevision() != Request.ExpectedSourceRevision ||
		DestinationRecord.GetRevision() !=
			Request.ExpectedDestinationRevision)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::RevisionConflict);
	}
	if (SourceRecord.GetLocation().ContainerType !=
			ERPGItemContainerType::Inventory ||
		DestinationRecord.GetLocation().ContainerType !=
			ERPGItemContainerType::Inventory ||
		SourceRecord.GetLocation().ContainerId !=
			DestinationRecord.GetLocation().ContainerId)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidContainerTransition);
	}
	if (SourceRecord.GetDefinitionId() !=
			DestinationRecord.GetDefinitionId() ||
		SourceRecord.GetDefinitionVersion() !=
			DestinationRecord.GetDefinitionVersion() ||
		!FRPGItemStackPolicy::AreInstanceStatesCompatible(
			SourceRecord.GetState(),
			DestinationRecord.GetState()))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::IncompatibleStack);
	}

	FRPGItemDefinitionSnapshot DefinitionSnapshot;
	if (!DefinitionCatalog.TryFindDefinition(
		SourceRecord.GetDefinitionId(),
		DefinitionSnapshot))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::DefinitionUnavailable);
	}
	if (DefinitionSnapshot.DefinitionVersion !=
		SourceRecord.GetDefinitionVersion())
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::DefinitionVersionMismatch);
	}

	const int32 DestinationCapacity = FMath::Max(
		0,
		DefinitionSnapshot.MaxStackSize -
			DestinationRecord.GetQuantity());
	const int32 TransferredQuantity = FMath::Min3(
		Request.RequestedQuantity,
		SourceRecord.GetQuantity(),
		DestinationCapacity);
	if (TransferredQuantity <= 0)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::StackFull);
	}

	FRPGItemRecord NewSourceRecord;
	const int32 SourceRemaining =
		SourceRecord.GetQuantity() - TransferredQuantity;
	const bool bSourceChanged = SourceRemaining > 0
		? SourceRecord.TrySetQuantity(SourceRemaining, NewSourceRecord)
		: SourceRecord.TryMarkConsumed(NewSourceRecord);

	FRPGItemRecord NewDestinationRecord;
	const bool bDestinationChanged = DestinationRecord.TrySetQuantity(
		DestinationRecord.GetQuantity() + TransferredQuantity,
		NewDestinationRecord);
	if (!bSourceChanged || !bDestinationChanged)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::RepositoryRejected);
	}

	FRPGItemRepositoryCommitRequest CommitRequest;
	CommitRequest.RequestId = Request.RequestId;
	CommitRequest.Operation = StackTransferOperation;
	CommitRequest.CommandFingerprint = CommandFingerprint;
	CommitRequest.Actor = Request.Actor;
	CommitRequest.AffectedQuantity = TransferredQuantity;

	FRPGItemRecordMutation& SourceMutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	SourceMutation.ExpectedRevision = Request.ExpectedSourceRevision;
	SourceMutation.NewRecord = MoveTemp(NewSourceRecord);

	FRPGItemRecordMutation& DestinationMutation =
		CommitRequest.Mutations.AddDefaulted_GetRef();
	DestinationMutation.ExpectedRevision =
		Request.ExpectedDestinationRevision;
	DestinationMutation.NewRecord = MoveTemp(NewDestinationRecord);

	return FromCommitResult(Repository.Commit(CommitRequest));
}

FRPGItemTransactionResult FRPGItemTransactionService::EquipItem(
	const FRPGItemEquipRequest& Request)
{
	FRPGItemCommandDescriptor Descriptor;
	if (!FRPGItemActionCommandPlanner::TryDescribe(
		Request,
		Descriptor))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemTransactionResult ReplayResult;
	if (TryReplay(
		Request.RequestId,
		Request.Actor,
		Descriptor.Operation,
		Descriptor.CommandFingerprint,
		ReplayResult))
	{
		return ReplayResult;
	}

	if (!ActionPolicyCatalog)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::DefinitionUnavailable);
	}

	const FRPGItemActionCommandPlanner Planner(
		Repository,
		DefinitionCatalog,
		*ActionPolicyCatalog,
		*Clock);
	const FRPGItemCommandPlan Plan = Planner.Prepare(Request);
	if (!Plan.IsReady())
	{
		return MakeFailure(Request.RequestId, Plan.FailureStatus);
	}
	return FromCommitResult(Repository.Commit(Plan.CommitRequest));
}

FRPGItemTransactionResult FRPGItemTransactionService::UnequipItem(
	const FRPGItemUnequipRequest& Request)
{
	FRPGItemCommandDescriptor Descriptor;
	if (!FRPGItemActionCommandPlanner::TryDescribe(
		Request,
		Descriptor))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemTransactionResult ReplayResult;
	if (TryReplay(
		Request.RequestId,
		Request.Actor,
		Descriptor.Operation,
		Descriptor.CommandFingerprint,
		ReplayResult))
	{
		return ReplayResult;
	}

	if (!ActionPolicyCatalog)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::DefinitionUnavailable);
	}

	const FRPGItemActionCommandPlanner Planner(
		Repository,
		DefinitionCatalog,
		*ActionPolicyCatalog,
		*Clock);
	const FRPGItemCommandPlan Plan = Planner.Prepare(Request);
	if (!Plan.IsReady())
	{
		return MakeFailure(Request.RequestId, Plan.FailureStatus);
	}
	return FromCommitResult(Repository.Commit(Plan.CommitRequest));
}

FRPGItemTransactionResult FRPGItemTransactionService::ConsumeItem(
	const FRPGItemConsumeRequest& Request)
{
	FRPGItemCommandDescriptor Descriptor;
	if (!FRPGItemActionCommandPlanner::TryDescribe(
		Request,
		Descriptor))
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::InvalidRequest);
	}

	FRPGItemTransactionResult ReplayResult;
	if (TryReplay(
		Request.RequestId,
		Request.Actor,
		Descriptor.Operation,
		Descriptor.CommandFingerprint,
		ReplayResult))
	{
		return ReplayResult;
	}

	if (!ActionPolicyCatalog)
	{
		return MakeFailure(
			Request.RequestId,
			ERPGItemTransactionStatus::DefinitionUnavailable);
	}

	const FRPGItemActionCommandPlanner Planner(
		Repository,
		DefinitionCatalog,
		*ActionPolicyCatalog,
		*Clock);
	const FRPGItemCommandPlan Plan = Planner.Prepare(Request);
	if (!Plan.IsReady())
	{
		return MakeFailure(Request.RequestId, Plan.FailureStatus);
	}
	return FromCommitResult(Repository.Commit(Plan.CommitRequest));
}

bool FRPGItemTransactionService::TryReplay(
	const FGuid& RequestId,
	const FRPGItemOwnerRef& Actor,
	const FName& Operation,
	const FString& CommandFingerprint,
	FRPGItemTransactionResult& OutResult) const
{
	FRPGItemRepositoryCommitResult CommitResult;
	if (!Repository.TryGetCommitResult(RequestId, CommitResult))
	{
		return false;
	}

	if (CommitResult.Actor != Actor ||
		CommitResult.Operation != Operation ||
		CommitResult.CommandFingerprint != CommandFingerprint)
	{
		OutResult = MakeFailure(
			RequestId,
			ERPGItemTransactionStatus::IdempotencyConflict);
		return true;
	}

	OutResult = FromCommitResult(CommitResult);
	return true;
}

FRPGItemTransactionResult FRPGItemTransactionService::FromCommitResult(
	const FRPGItemRepositoryCommitResult& CommitResult)
{
	FRPGItemTransactionResult Result;
	Result.RequestId = CommitResult.RequestId;
	Result.AffectedQuantity = CommitResult.AffectedQuantity;
	Result.Records = CommitResult.Records;

	switch (CommitResult.Status)
	{
	case ERPGItemRepositoryCommitStatus::Committed:
		Result.Status = ERPGItemTransactionStatus::Succeeded;
		break;
	case ERPGItemRepositoryCommitStatus::AlreadyCommitted:
		Result.Status = ERPGItemTransactionStatus::AlreadyApplied;
		break;
	case ERPGItemRepositoryCommitStatus::InvalidRequest:
	case ERPGItemRepositoryCommitStatus::ValidationFailed:
		Result.Status = ERPGItemTransactionStatus::InvalidRequest;
		break;
	case ERPGItemRepositoryCommitStatus::NotFound:
		Result.Status = ERPGItemTransactionStatus::NotFound;
		break;
	case ERPGItemRepositoryCommitStatus::RevisionConflict:
		Result.Status = ERPGItemTransactionStatus::RevisionConflict;
		break;
	case ERPGItemRepositoryCommitStatus::LocationConflict:
		Result.Status = ERPGItemTransactionStatus::LocationOccupied;
		break;
	default:
		Result.Status = ERPGItemTransactionStatus::RepositoryRejected;
		break;
	}
	return Result;
}

FRPGItemTransactionResult FRPGItemTransactionService::MakeFailure(
	const FGuid& RequestId,
	const ERPGItemTransactionStatus Status)
{
	FRPGItemTransactionResult Result;
	Result.RequestId = RequestId;
	Result.Status = Status;
	return Result;
}
