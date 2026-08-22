#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRecord.h"

enum class ERPGItemRepositoryCommitStatus : uint8
{
	Committed,
	AlreadyCommitted,
	InvalidRequest,
	NotFound,
	RevisionConflict,
	LocationConflict,
	ValidationFailed,
	InternalError
};

struct PROJECT_RPG_API FRPGItemRecordMutation
{
	/**
	 * Zero means the item must not exist. Positive values perform compare-and-
	 * swap against the stored revision.
	 */
	int64 ExpectedRevision = 0;
	FRPGItemRecord NewRecord;
};

struct PROJECT_RPG_API FRPGItemRepositoryCommitRequest
{
	FGuid RequestId;
	FName Operation;
	FString CommandFingerprint;
	FRPGItemOwnerRef Actor;
	int32 AffectedQuantity = 0;
	TArray<FRPGItemRecordMutation> Mutations;
};

struct PROJECT_RPG_API FRPGItemRepositoryCommitResult
{
	ERPGItemRepositoryCommitStatus Status =
		ERPGItemRepositoryCommitStatus::InternalError;
	FGuid RequestId;
	FName Operation;
	FString CommandFingerprint;
	FRPGItemOwnerRef Actor;
	int32 AffectedQuantity = 0;
	TArray<FRPGItemRecord> Records;

	bool WasApplied() const
	{
		return Status == ERPGItemRepositoryCommitStatus::Committed ||
			Status == ERPGItemRepositoryCommitStatus::AlreadyCommitted;
	}
};

/** Read-only authoritative record source used while planning commands. */
class PROJECT_RPG_API IRPGItemRecordSource
{
public:
	virtual ~IRPGItemRecordSource() = default;

	virtual bool Find(
		const FGuid& ItemId,
		FRPGItemRecord& OutRecord) const = 0;
	virtual bool FindAtLocation(
		const FRPGItemOwnerRef& Owner,
		const FRPGItemLocation& Location,
		FRPGItemRecord& OutRecord) const = 0;
	virtual void FindByOwner(
		const FRPGItemOwnerRef& Owner,
		TArray<FRPGItemRecord>& OutRecords) const = 0;
};

/**
 * Persistence boundary for authoritative item transactions.
 *
 * Commit must atomically validate every expected revision, enforce unique
 * active locations, write all mutations, and persist the idempotency receipt.
 */
class PROJECT_RPG_API IRPGItemRepository : public IRPGItemRecordSource
{
public:
	virtual ~IRPGItemRepository() = default;

	virtual bool TryGetCommitResult(
		const FGuid& RequestId,
		FRPGItemRepositoryCommitResult& OutResult) const = 0;
	virtual FRPGItemRepositoryCommitResult Commit(
		const FRPGItemRepositoryCommitRequest& Request) = 0;
};
