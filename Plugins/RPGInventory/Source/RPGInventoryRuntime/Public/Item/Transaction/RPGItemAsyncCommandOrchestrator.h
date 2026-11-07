#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"

class FRPGInventoryCommandJournal;

class RPGINVENTORYRUNTIME_API IRPGItemAsyncCommitter
{
public:
	virtual ~IRPGItemAsyncCommitter() = default;

	/** Completion must be invoked exactly once, including immediate rejection. */
	virtual bool Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) = 0;
};

class RPGINVENTORYRUNTIME_API IRPGItemCommitSink
{
public:
	virtual ~IRPGItemCommitSink() = default;
	virtual bool ApplyCommit(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError) = 0;
};

class RPGINVENTORYRUNTIME_API IRPGItemFirstCommitSink
{
public:
	virtual ~IRPGItemFirstCommitSink() = default;

	/** Opt in only with an original, avatar-bound, idempotent consume effect sink. */
	virtual bool SupportsScopedConsumeRecovery() const { return false; }
	virtual bool PrepareConsume(const FRPGItemRepositoryCommitRequest& Request, FString& OutError) { return true; }
	virtual void DiscardConsume(const FGuid& RequestId) {}
	/** Succeeded, or scoped Consume AlreadyApplied; the latter must not repeat a delivered effect. */
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
	Completed,
	Uncertain,
	PersistenceRejected,
	PreparationRejected
};

struct RPGINVENTORYRUNTIME_API FRPGItemAsyncCommandOutcome
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
class RPGINVENTORYRUNTIME_API FRPGItemAsyncCommandOrchestrator final
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
		IRPGItemFirstCommitSink& InFirstCommitSink,
		TSharedPtr<FRPGInventoryCommandJournal> InInventoryJournal = {});

	bool Move(
		const FRPGItemMoveRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Swap(
		const FRPGItemSwapRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool TransferStack(
		const FRPGItemStackTransferRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Split(const FRPGItemSplitRequest& Request, FRPGItemAsyncCommandCompletion Completion);
	bool Sort(const FGuid& RequestId, const FRPGItemOwnerRef& Owner, EItemCategory Category,
		FRPGItemAsyncCommandCompletion Completion);
	bool Equip(
		const FRPGItemEquipRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Unequip(
		const FRPGItemUnequipRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool Consume(
		const FRPGItemConsumeRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);
	bool ClaimMail(
		const FRPGItemClaimMailRequest& Request,
		FRPGItemAsyncCommandCompletion Completion);

	/** Resubmit the frozen server plan (including journal restores), never replan client input. */
	bool RetryInventoryCommand(const FGuid& RequestId, const FRPGItemOwnerRef& Owner,
		FRPGItemAsyncCommandCompletion Completion);
	bool FindUncertainInventoryCommand(const FGuid& RequestId, const FRPGItemOwnerRef& Owner, FName& Operation) const;
	bool HasUncertainInventoryCommands() const { return !UncertainInventoryRequests.IsEmpty(); }
	bool IsInventoryPersistenceBlocked() const;

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
	bool DispatchFrozen(const FRPGItemRepositoryCommitRequest& Request, FRPGItemAsyncCommandCompletion Completion);
	bool IsRecoverableOperation(FName Operation) const;
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
	TMap<FGuid, FRPGItemRepositoryCommitRequest> UncertainInventoryRequests;
	TSet<FGuid> InventoryRetriesInFlight;
	TSharedPtr<FRPGInventoryCommandJournal> InventoryJournal;
	bool bPersistenceFailure = false;
	// Server-created effect domain; never restored from a client or the bag journal.
	const FGuid ConsumeEffectScope = FGuid::NewGuid();
};
