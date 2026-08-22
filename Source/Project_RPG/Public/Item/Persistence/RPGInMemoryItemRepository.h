#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRepository.h"

/**
 * Thread-safe reference implementation used by automation tests and offline
 * servers. Production shards should implement the same contract in their DB
 * adapter so revisions and idempotency receipts survive process restarts.
 */
class PROJECT_RPG_API FRPGInMemoryItemRepository final
	: public IRPGItemRepository
{
public:
	virtual bool Find(
		const FGuid& ItemId,
		FRPGItemRecord& OutRecord) const override;
	virtual bool FindAtLocation(
		const FRPGItemOwnerRef& Owner,
		const FRPGItemLocation& Location,
		FRPGItemRecord& OutRecord) const override;
	virtual void FindByOwner(
		const FRPGItemOwnerRef& Owner,
		TArray<FRPGItemRecord>& OutRecords) const override;
	virtual bool TryGetCommitResult(
		const FGuid& RequestId,
		FRPGItemRepositoryCommitResult& OutResult) const override;
	virtual FRPGItemRepositoryCommitResult Commit(
		const FRPGItemRepositoryCommitRequest& Request) override;

private:
	mutable FCriticalSection CriticalSection;
	TMap<FGuid, FRPGItemRecord> Records;
	TMap<FGuid, FRPGItemRepositoryCommitResult> CommitResults;
};
