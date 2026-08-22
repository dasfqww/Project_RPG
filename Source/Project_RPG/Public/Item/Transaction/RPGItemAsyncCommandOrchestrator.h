#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"

class PROJECT_RPG_API IRPGItemAsyncCommitter
{
public:
	virtual ~IRPGItemAsyncCommitter() = default;

	/** Completion must be invoked exactly once, including immediate rejection. */
	virtual bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) = 0;
};

class PROJECT_RPG_API IRPGItemCommitSink
{
public:
	virtual ~IRPGItemCommitSink() = default;
	virtual bool ApplyCommit(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) = 0;
};

class PROJECT_RPG_API IRPGItemFirstCommitSink
{
public:
	virtual ~IRPGItemFirstCommitSink() = default;

	/** Called only for the first Succeeded receipt, never AlreadyApplied. */
	virtual bool EnqueueFirstCommit(
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) = 0;
};

enum class ERPGItemAsyncCommandStage : uint8
{
	PlanRejected,
	BackendRejected,
	ProtocolRejected,
	ProjectionRejected,
	PostCommitRejected,
	Completed
};

struct PROJECT_RPG_API FRPGItemAsyncCommandOutcome
{
	ERPGItemAsyncCommandStage Stage =
		ERPGItemAsyncCommandStage::PlanRejected;
	ERPGItemTransactionStatus PlanFailureStatus =
		ERPGItemTransactionStatus::InvalidRequest;
	FRPGItemBackendCommitResult BackendResult;
	FString Error;

	bool WasSuccessful() const
	{
		return Stage == ERPGItemAsyncCommandStage::Completed;
	}
};

using FRPGItemAsyncCommandCompletion =
	TFunction<void(FRPGItemAsyncCommandOutcome)>;

/** Plans locally, commits asynchronously, then updates authoritative sinks. */
class PROJECT_RPG_API FRPGItemAsyncCommandOrchestrator final
	: public TSharedFromThis<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe>
{
public:
	FRPGItemAsyncCommandOrchestrator(
		const IRPGItemRecordSource& InRecords,
		const IRPGItemDefinitionCatalog& InDefinitions,
		const IRPGItemActionPolicyCatalog& InActions,
		IRPGItemAsyncCommitter& InCommitter,
		IRPGItemCommitSink& InCommitSink,
		IRPGItemFirstCommitSink& InFirstCommitSink);

	bool Equip(
		const FRPGItemEquipRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Unequip(
		const FRPGItemUnequipRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Consume(
		const FRPGItemConsumeRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);

private:
	struct FCachedCompletion
	{
		FRPGItemCommandDescriptor Descriptor;
		FRPGItemBackendCommitResult Result;
	};

	bool TryCompleteFromCache(
		const FGuid& RequestId,
		const FRPGItemCommandDescriptor& Descriptor,
		FRPGItemAsyncCommandCompletion& Completion);
	bool Submit(
		FRPGItemCommandPlan&& Plan,
		FRPGItemAsyncCommandCompletion Completion);
	void HandleCommit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemAsyncCommandCompletion Completion,
		FRPGItemBackendCommitResult Result);

	FRPGItemActionCommandPlanner Planner;
	IRPGItemAsyncCommitter& Committer;
	IRPGItemCommitSink& CommitSink;
	IRPGItemFirstCommitSink& FirstCommitSink;
	TMap<FGuid, FCachedCompletion> CompletedRequests;
	TArray<FGuid> CompletedRequestOrder;
};
