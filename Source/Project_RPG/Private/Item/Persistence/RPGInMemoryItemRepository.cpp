#include "Item/Persistence/RPGInMemoryItemRepository.h"

#include "Misc/ScopeLock.h"

namespace
{
struct FRPGActiveItemLocationKey
{
	FRPGItemOwnerRef Owner;
	FRPGItemLocation Location;

	friend bool operator==(
		const FRPGActiveItemLocationKey& Left,
		const FRPGActiveItemLocationKey& Right)
	{
		return Left.Owner == Right.Owner && Left.Location == Right.Location;
	}

	friend uint32 GetTypeHash(const FRPGActiveItemLocationKey& Key)
	{
		return HashCombine(GetTypeHash(Key.Owner), GetTypeHash(Key.Location));
	}
};

FRPGItemRepositoryCommitResult MakeResult(
	const FRPGItemRepositoryCommitRequest& Request,
	const ERPGItemRepositoryCommitStatus Status)
{
	FRPGItemRepositoryCommitResult Result;
	Result.Status = Status;
	Result.RequestId = Request.RequestId;
	Result.Operation = Request.Operation;
	Result.CommandFingerprint = Request.CommandFingerprint;
	Result.Actor = Request.Actor;
	Result.AffectedQuantity = Request.AffectedQuantity;
	return Result;
}
}

bool FRPGInMemoryItemRepository::Find(
	const FGuid& ItemId,
	FRPGItemRecord& OutRecord) const
{
	FScopeLock Lock(&CriticalSection);
	const FRPGItemRecord* Record = Records.Find(ItemId);
	if (!Record)
	{
		return false;
	}

	OutRecord = *Record;
	return true;
}

bool FRPGInMemoryItemRepository::FindAtLocation(
	const FRPGItemOwnerRef& Owner,
	const FRPGItemLocation& Location,
	FRPGItemRecord& OutRecord) const
{
	FScopeLock Lock(&CriticalSection);
	for (const TPair<FGuid, FRPGItemRecord>& Pair : Records)
	{
		if (Pair.Value.IsActive() &&
			Pair.Value.GetOwner() == Owner &&
			Pair.Value.GetLocation() == Location)
		{
			OutRecord = Pair.Value;
			return true;
		}
	}
	return false;
}

void FRPGInMemoryItemRepository::FindByOwner(
	const FRPGItemOwnerRef& Owner,
	TArray<FRPGItemRecord>& OutRecords) const
{
	FScopeLock Lock(&CriticalSection);
	OutRecords.Reset();
	for (const TPair<FGuid, FRPGItemRecord>& Pair : Records)
	{
		if (Pair.Value.GetOwner() == Owner)
		{
			OutRecords.Add(Pair.Value);
		}
	}

	OutRecords.Sort([](
		const FRPGItemRecord& Left,
		const FRPGItemRecord& Right)
	{
		const FRPGItemLocation& LeftLocation = Left.GetLocation();
		const FRPGItemLocation& RightLocation = Right.GetLocation();
		if (LeftLocation.ContainerType != RightLocation.ContainerType)
		{
			return static_cast<uint8>(LeftLocation.ContainerType) <
				static_cast<uint8>(RightLocation.ContainerType);
		}
		if (LeftLocation.ContainerId != RightLocation.ContainerId)
		{
			return LeftLocation.ContainerId < RightLocation.ContainerId;
		}
		return LeftLocation.SlotIndex < RightLocation.SlotIndex;
	});
}

bool FRPGInMemoryItemRepository::TryGetCommitResult(
	const FGuid& RequestId,
	FRPGItemRepositoryCommitResult& OutResult) const
{
	FScopeLock Lock(&CriticalSection);
	const FRPGItemRepositoryCommitResult* Result =
		CommitResults.Find(RequestId);
	if (!Result)
	{
		return false;
	}

	OutResult = *Result;
	OutResult.Status = ERPGItemRepositoryCommitStatus::AlreadyCommitted;
	return true;
}

FRPGItemRepositoryCommitResult FRPGInMemoryItemRepository::Commit(
	const FRPGItemRepositoryCommitRequest& Request)
{
	FScopeLock Lock(&CriticalSection);

	if (const FRPGItemRepositoryCommitResult* ExistingResult =
		CommitResults.Find(Request.RequestId))
	{
		FRPGItemRepositoryCommitResult Result = *ExistingResult;
		Result.Status = ERPGItemRepositoryCommitStatus::AlreadyCommitted;
		return Result;
	}

	if (!Request.RequestId.IsValid() ||
		Request.Operation.IsNone() ||
		Request.CommandFingerprint.IsEmpty() ||
		!Request.Actor.IsValid() ||
		Request.AffectedQuantity < 0 ||
		Request.Mutations.IsEmpty())
	{
		return MakeResult(
			Request,
			ERPGItemRepositoryCommitStatus::InvalidRequest);
	}

	TSet<FGuid> MutatedItemIds;
	for (const FRPGItemRecordMutation& Mutation : Request.Mutations)
	{
		const FGuid& ItemId = Mutation.NewRecord.GetItemId();
		if (!ItemId.IsValid() ||
			Mutation.ExpectedRevision < 0 ||
			Mutation.NewRecord.GetRevision() != Mutation.ExpectedRevision ||
			MutatedItemIds.Contains(ItemId))
		{
			return MakeResult(
				Request,
				ERPGItemRepositoryCommitStatus::InvalidRequest);
		}
		MutatedItemIds.Add(ItemId);

		const FRPGItemRecord* StoredRecord = Records.Find(ItemId);
		if (Mutation.ExpectedRevision == 0)
		{
			if (StoredRecord)
			{
				return MakeResult(
					Request,
					ERPGItemRepositoryCommitStatus::RevisionConflict);
			}
		}
		else
		{
			if (!StoredRecord)
			{
				return MakeResult(
					Request,
					ERPGItemRepositoryCommitStatus::NotFound);
			}
			if (StoredRecord->GetRevision() != Mutation.ExpectedRevision)
			{
				return MakeResult(
					Request,
					ERPGItemRepositoryCommitStatus::RevisionConflict);
			}
		}
	}

	TMap<FGuid, FRPGItemRecord> CandidateRecords = Records;
	FRPGItemRepositoryCommitResult Result = MakeResult(
		Request,
		ERPGItemRepositoryCommitStatus::Committed);
	Result.Records.Reserve(Request.Mutations.Num());

	for (const FRPGItemRecordMutation& Mutation : Request.Mutations)
	{
		FRPGItemRecord WrittenRecord =
			Mutation.NewRecord.CopyWithRevision(
				Mutation.ExpectedRevision + 1);
		if (!WrittenRecord.IsStructurallyValid())
		{
			return MakeResult(
				Request,
				ERPGItemRepositoryCommitStatus::ValidationFailed);
		}

		CandidateRecords.Add(WrittenRecord.GetItemId(), WrittenRecord);
		Result.Records.Add(WrittenRecord);
	}

	TMap<FRPGActiveItemLocationKey, FGuid> OccupiedLocations;
	for (const TPair<FGuid, FRPGItemRecord>& Pair : CandidateRecords)
	{
		const FRPGItemRecord& Record = Pair.Value;
		if (!Record.IsActive())
		{
			continue;
		}

		FRPGActiveItemLocationKey Key;
		Key.Owner = Record.GetOwner();
		Key.Location = Record.GetLocation();
		if (const FGuid* ExistingItemId = OccupiedLocations.Find(Key);
			ExistingItemId && *ExistingItemId != Record.GetItemId())
		{
			return MakeResult(
				Request,
				ERPGItemRepositoryCommitStatus::LocationConflict);
		}
		OccupiedLocations.Add(Key, Record.GetItemId());
	}

	Records = MoveTemp(CandidateRecords);
	CommitResults.Add(Request.RequestId, Result);
	return Result;
}
