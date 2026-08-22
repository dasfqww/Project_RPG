#include "Item/Projection/RPGInventoryProjectionStore.h"

#include "Item/Projection/RPGInventoryProjectionMapper.h"

namespace RPGInventoryProjectionStore
{
void SetError(FString* OutError, const TCHAR* Message)
{
	if (OutError)
	{
		*OutError = Message;
	}
}
}

bool FRPGInventoryProjectionStore::Replace(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& Records,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	TMap<FGuid, FRPGItemRecord> CandidateRecords;
	CandidateRecords.Reserve(Records.Num());
	for (const FRPGItemRecord& Record : Records)
	{
		if (CandidateRecords.Contains(Record.GetItemId()))
		{
			RPGInventoryProjectionStore::SetError(
				OutError,
				TEXT("The authoritative item load contains a duplicate item ID."));
			OutEntries.Reset();
			return false;
		}
		CandidateRecords.Add(Record.GetItemId(), Record);
	}

	if (!TryBuildEntries(
		ExpectedOwner,
		CandidateRecords,
		OutEntries,
		OutError))
	{
		return false;
	}

	Owner = ExpectedOwner;
	RecordsById = MoveTemp(CandidateRecords);
	bInitialized = true;
	return true;
}

bool FRPGInventoryProjectionStore::ApplyMutations(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TArray<FRPGItemRecord>& MutationRecords,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	if (!bInitialized || Owner != ExpectedOwner)
	{
		RPGInventoryProjectionStore::SetError(
			OutError,
			TEXT("The projection cache is not initialized for this owner."));
		OutEntries.Reset();
		return false;
	}
	if (MutationRecords.IsEmpty())
	{
		RPGInventoryProjectionStore::SetError(
			OutError,
			TEXT("A successful item commit must contain mutation records."));
		OutEntries.Reset();
		return false;
	}

	TMap<FGuid, FRPGItemRecord> CandidateRecords = RecordsById;
	TSet<FGuid> MutationIds;
	for (const FRPGItemRecord& Record : MutationRecords)
	{
		if (!Record.GetItemId().IsValid() ||
			MutationIds.Contains(Record.GetItemId()))
		{
			RPGInventoryProjectionStore::SetError(
				OutError,
				TEXT("The item commit contains duplicate or invalid item IDs."));
			OutEntries.Reset();
			return false;
		}
		if (const FRPGItemRecord* Existing =
			CandidateRecords.Find(Record.GetItemId());
			Existing && Record.GetRevision() < Existing->GetRevision())
		{
			// An old idempotency receipt is already represented by a newer
			// local record. Treat it as a successful no-op.
			MutationIds.Add(Record.GetItemId());
			continue;
		}
		MutationIds.Add(Record.GetItemId());
		CandidateRecords.Add(Record.GetItemId(), Record);
	}

	if (!TryBuildEntries(
		ExpectedOwner,
		CandidateRecords,
		OutEntries,
		OutError))
	{
		return false;
	}

	RecordsById = MoveTemp(CandidateRecords);
	return true;
}

bool FRPGInventoryProjectionStore::Find(
	const FGuid& ItemId,
	FRPGItemRecord& OutRecord) const
{
	const FRPGItemRecord* Record = RecordsById.Find(ItemId);
	if (!bInitialized || !Record)
	{
		return false;
	}
	OutRecord = *Record;
	return true;
}

bool FRPGInventoryProjectionStore::FindAtLocation(
	const FRPGItemOwnerRef& ExpectedOwner,
	const FRPGItemLocation& Location,
	FRPGItemRecord& OutRecord) const
{
	if (!bInitialized || Owner != ExpectedOwner)
	{
		return false;
	}
	for (const TPair<FGuid, FRPGItemRecord>& Pair : RecordsById)
	{
		if (Pair.Value.IsActive() &&
			Pair.Value.GetOwner() == ExpectedOwner &&
			Pair.Value.GetLocation() == Location)
		{
			OutRecord = Pair.Value;
			return true;
		}
	}
	return false;
}

void FRPGInventoryProjectionStore::FindByOwner(
	const FRPGItemOwnerRef& ExpectedOwner,
	TArray<FRPGItemRecord>& OutRecords) const
{
	OutRecords.Reset();
	if (!bInitialized || Owner != ExpectedOwner)
	{
		return;
	}
	RecordsById.GenerateValueArray(OutRecords);
	OutRecords.Sort(
		[](const FRPGItemRecord& Left, const FRPGItemRecord& Right)
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

bool FRPGInventoryProjectionStore::TryBuildEntries(
	const FRPGItemOwnerRef& ExpectedOwner,
	const TMap<FGuid, FRPGItemRecord>& CandidateRecords,
	TArray<FRPGInventoryProjectionEntry>& OutEntries,
	FString* OutError)
{
	TArray<FRPGItemRecord> Records;
	CandidateRecords.GenerateValueArray(Records);
	return FRPGInventoryProjectionMapper::BuildInventorySnapshot(
		ExpectedOwner,
		Records,
		OutEntries,
		OutError);
}
