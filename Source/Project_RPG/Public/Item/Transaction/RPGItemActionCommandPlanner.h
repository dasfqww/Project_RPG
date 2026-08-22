#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRepository.h"
#include "Item/Transaction/RPGItemClock.h"
#include "Item/Transaction/RPGItemTransactionTypes.h"

class IRPGItemActionPolicyCatalog;
class IRPGItemDefinitionCatalog;
struct FRPGItemActionPolicy;
struct FRPGItemDefinitionSnapshot;

struct PROJECT_RPG_API FRPGItemCommandDescriptor
{
	FName Operation;
	FString CommandFingerprint;

	bool IsValid() const
	{
		return !Operation.IsNone() && !CommandFingerprint.IsEmpty();
	}
};

/** Result of validation before an external CAS commit is submitted. */
struct PROJECT_RPG_API FRPGItemCommandPlan
{
	bool bReady = false;
	ERPGItemTransactionStatus FailureStatus =
		ERPGItemTransactionStatus::InvalidRequest;
	FRPGItemRepositoryCommitRequest CommitRequest;

	bool IsReady() const
	{
		return bReady && CommitRequest.RequestId.IsValid();
	}
};

/**
 * Pure action-command planner shared by synchronous repositories and the
 * Dedicated Server asynchronous backend pipeline.
 */
class PROJECT_RPG_API FRPGItemActionCommandPlanner
{
public:
	FRPGItemActionCommandPlanner(
		const IRPGItemRecordSource& InRecords,
		const IRPGItemDefinitionCatalog& InDefinitions,
		const IRPGItemActionPolicyCatalog& InActions);
	FRPGItemActionCommandPlanner(
		const IRPGItemRecordSource& InRecords,
		const IRPGItemDefinitionCatalog& InDefinitions,
		const IRPGItemActionPolicyCatalog& InActions,
		const IRPGItemClock& InClock);

	static bool TryDescribe(
		const FRPGItemEquipRequest& Request,
		FRPGItemCommandDescriptor& OutDescriptor);
	static bool TryDescribe(
		const FRPGItemUnequipRequest& Request,
		FRPGItemCommandDescriptor& OutDescriptor);
	static bool TryDescribe(
		const FRPGItemConsumeRequest& Request,
		FRPGItemCommandDescriptor& OutDescriptor);

	FRPGItemCommandPlan Prepare(
		const FRPGItemEquipRequest& Request) const;
	FRPGItemCommandPlan Prepare(
		const FRPGItemUnequipRequest& Request) const;
	FRPGItemCommandPlan Prepare(
		const FRPGItemConsumeRequest& Request) const;

private:
	bool TryLoadMutableRecord(
		const FGuid& ItemId,
		const FRPGItemOwnerRef& Actor,
		int64 ExpectedRevision,
		FRPGItemRecord& OutRecord,
		ERPGItemTransactionStatus& OutFailureStatus) const;
	bool TryResolveActionPolicy(
		const FRPGItemRecord& Record,
		FRPGItemDefinitionSnapshot& OutDefinition,
		FRPGItemActionPolicy& OutPolicy,
		ERPGItemTransactionStatus& OutFailureStatus) const;

	static FRPGItemCommandPlan Failure(
		ERPGItemTransactionStatus Status);
	static FRPGItemCommandPlan Ready(
		FRPGItemRepositoryCommitRequest&& CommitRequest);

	const IRPGItemRecordSource& Records;
	const IRPGItemDefinitionCatalog& Definitions;
	const IRPGItemActionPolicyCatalog& Actions;
	FRPGSystemItemClock SystemClock;
	const IRPGItemClock* Clock = nullptr;
};
