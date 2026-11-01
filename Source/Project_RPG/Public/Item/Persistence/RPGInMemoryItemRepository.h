#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Item/Persistence/RPGItemRepository.h"

/**
 * Thread-safe repository reference implementation for automation fixtures only.
 * Online gameplay commits through the backend; this is not a durable store.
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

#endif // WITH_DEV_AUTOMATION_TESTS
