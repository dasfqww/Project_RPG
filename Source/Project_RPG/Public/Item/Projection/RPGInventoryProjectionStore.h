#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRepository.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"

/**
 * Server-only authoritative cache behind the replicated inventory read model.
 * Full loads replace the cache; successful Commit records update it by ItemId.
 */
class PROJECT_RPG_API FRPGInventoryProjectionStore final
	: public IRPGItemRecordSource
{
public:
	bool Replace(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& Records,
		TArray<FRPGInventoryProjectionEntry>& OutEntries,
		FString* OutError = nullptr);

	bool ApplyMutations(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& MutationRecords,
		TArray<FRPGInventoryProjectionEntry>& OutEntries,
		FString* OutError = nullptr);

	bool IsInitialized() const { return bInitialized; }
	const FRPGItemOwnerRef& GetOwner() const { return Owner; }
	int32 NumAuthoritativeRecords() const { return RecordsById.Num(); }

	virtual bool Find(
		const FGuid& ItemId,
		FRPGItemRecord& OutRecord) const override;
	virtual bool FindAtLocation(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemLocation& Location,
		FRPGItemRecord& OutRecord) const override;
	virtual void FindByOwner(
		const FRPGItemOwnerRef& ExpectedOwner,
		TArray<FRPGItemRecord>& OutRecords) const override;

private:
	static bool TryBuildEntries(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TMap<FGuid, FRPGItemRecord>& CandidateRecords,
		TArray<FRPGInventoryProjectionEntry>& OutEntries,
		FString* OutError);

	bool bInitialized = false;
	FRPGItemOwnerRef Owner;
	TMap<FGuid, FRPGItemRecord> RecordsById;
};
