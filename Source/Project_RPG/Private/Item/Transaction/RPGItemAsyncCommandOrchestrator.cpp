#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"

FRPGItemAsyncCommandOrchestrator::FRPGItemAsyncCommandOrchestrator(
	const IRPGItemRecordSource& InRecords,
	const IRPGItemDefinitionCatalog& InDefinitions,
	const IRPGItemActionPolicyCatalog& InActions,
	IRPGItemAsyncCommitter& InCommitter,
	IRPGItemCommitSink& InCommitSink,
	IRPGItemFirstCommitSink& InFirstCommitSink)
	: Planner(InRecords, InDefinitions, InActions)
	, Committer(InCommitter)
	, CommitSink(InCommitSink)
	, FirstCommitSink(InFirstCommitSink)
{
}

bool FRPGItemAsyncCommandOrchestrator::Equip(
	const FRPGItemEquipRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(
			Request.RequestId,
			Descriptor,
			Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::Unequip(
	const FRPGItemUnequipRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(
			Request.RequestId,
			Descriptor,
			Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::Consume(
	const FRPGItemConsumeRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(
			Request.RequestId,
			Descriptor,
			Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::TryCompleteFromCache(
	const FGuid& RequestId,
	const FRPGItemCommandDescriptor& Descriptor,
	FRPGItemAsyncCommandCompletion& Completion)
{
	const FCachedCompletion* Cached = CompletedRequests.Find(RequestId);
	if (!Cached)
	{
		return false;
	}

	FRPGItemAsyncCommandOutcome Outcome;
	if (Cached->Descriptor.Operation != Descriptor.Operation ||
		Cached->Descriptor.CommandFingerprint !=
			Descriptor.CommandFingerprint)
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::PlanRejected;
		Outcome.PlanFailureStatus =
			ERPGItemTransactionStatus::IdempotencyConflict;
	}
	else
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::Completed;
		Outcome.BackendResult = Cached->Result;
		Outcome.BackendResult.Status =
			ERPGItemBackendStatus::AlreadyApplied;
	}
	Completion(MoveTemp(Outcome));
	return true;
}

bool FRPGItemAsyncCommandOrchestrator::Submit(
	FRPGItemCommandPlan&& Plan,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	if (!Plan.IsReady())
	{
		FRPGItemAsyncCommandOutcome Outcome;
		Outcome.Stage = ERPGItemAsyncCommandStage::PlanRejected;
		Outcome.PlanFailureStatus = Plan.FailureStatus;
		Completion(MoveTemp(Outcome));
		return false;
	}

	const FRPGItemRepositoryCommitRequest Request =
		MoveTemp(Plan.CommitRequest);
	const TWeakPtr<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe> WeakThis = AsShared();
	return Committer.Commit(
		Request,
		[WeakThis, Request, Completion = MoveTemp(Completion)](
			FRPGItemBackendCommitResult Result) mutable
		{
			const TSharedPtr<
				FRPGItemAsyncCommandOrchestrator,
				ESPMode::ThreadSafe> Self = WeakThis.Pin();
			if (!Self)
			{
				return;
			}
			Self->HandleCommit(
				Request,
				MoveTemp(Completion),
				MoveTemp(Result));
		});
}

void FRPGItemAsyncCommandOrchestrator::HandleCommit(
	const FRPGItemRepositoryCommitRequest& Request,
	FRPGItemAsyncCommandCompletion Completion,
	FRPGItemBackendCommitResult Result)
{
	FRPGItemAsyncCommandOutcome Outcome;
	Outcome.BackendResult = Result;
	if (Result.RequestId != Request.RequestId ||
		Result.Operation != Request.Operation ||
		Result.CommandFingerprint != Request.CommandFingerprint ||
		Result.Actor != Request.Actor)
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::ProtocolRejected;
		Outcome.BackendResult.Status =
			ERPGItemBackendStatus::ProtocolError;
		Outcome.Error = TEXT(
			"The backend commit receipt does not match the planned command.");
		Completion(MoveTemp(Outcome));
		return;
	}
	if (!Result.WasSuccessful())
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::BackendRejected;
		Outcome.Error = Result.Error;
		Completion(MoveTemp(Outcome));
		return;
	}
	if (Result.Records.IsEmpty())
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::ProtocolRejected;
		Outcome.Error = TEXT(
			"A successful item commit receipt contains no records.");
		Completion(MoveTemp(Outcome));
		return;
	}

	FString SinkError;
	if (!CommitSink.ApplyCommit(Request.Actor, Result, SinkError))
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::ProjectionRejected;
		Outcome.Error = MoveTemp(SinkError);
		Completion(MoveTemp(Outcome));
		return;
	}

	if (Result.Status == ERPGItemBackendStatus::Succeeded &&
		!FirstCommitSink.EnqueueFirstCommit(Result, SinkError))
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::PostCommitRejected;
		Outcome.Error = MoveTemp(SinkError);
		Completion(MoveTemp(Outcome));
		return;
	}

	Outcome.Stage = ERPGItemAsyncCommandStage::Completed;
	FCachedCompletion Cached;
	Cached.Descriptor.Operation = Request.Operation;
	Cached.Descriptor.CommandFingerprint = Request.CommandFingerprint;
	Cached.Result = Result;
	CompletedRequests.Add(Request.RequestId, MoveTemp(Cached));
	CompletedRequestOrder.Add(Request.RequestId);
	constexpr int32 MaximumCompletedRequests = 256;
	if (CompletedRequestOrder.Num() > MaximumCompletedRequests)
	{
		CompletedRequests.Remove(CompletedRequestOrder[0]);
		CompletedRequestOrder.RemoveAt(0, 1, EAllowShrinking::No);
	}
	Completion(MoveTemp(Outcome));
}
