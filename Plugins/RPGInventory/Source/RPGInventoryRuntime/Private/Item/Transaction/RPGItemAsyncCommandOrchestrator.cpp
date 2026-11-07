#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"

bool FRPGItemAsyncCommandOrchestrator::IsRecoverableOperation(const FName Operation) const
{
	return FRPGInventoryCommandJournal::Supports(Operation) ||
		(Operation == TEXT("Item.Consume") && FirstCommitSink.SupportsScopedConsumeRecovery());
}

FRPGItemAsyncCommandOrchestrator::FRPGItemAsyncCommandOrchestrator(
	const IRPGItemRecordSource& InRecords,
	const IRPGItemDefinitionCatalog& InDefinitions,
	const IRPGItemActionPolicyCatalog& InActions,
	IRPGItemAsyncCommitter& InCommitter,
	IRPGItemCommitSink& InCommitSink,
	IRPGItemFirstCommitSink& InFirstCommitSink,
	TSharedPtr<FRPGInventoryCommandJournal> InInventoryJournal)
	: Planner(InRecords, InDefinitions, InActions)
	, Committer(InCommitter)
	, CommitSink(InCommitSink)
	, FirstCommitSink(InFirstCommitSink)
	, InventoryJournal(MoveTemp(InInventoryJournal))
{
	if (InventoryJournal && InventoryJournal->IsHealthy() && !InventoryJournal->IsRevoked())
		UncertainInventoryRequests = InventoryJournal->GetPending();
}

bool FRPGItemAsyncCommandOrchestrator::IsInventoryPersistenceBlocked() const
{
	return bPersistenceFailure || (InventoryJournal && (!InventoryJournal->IsHealthy() || InventoryJournal->IsRevoked()));
}

bool FRPGItemAsyncCommandOrchestrator::Move(
	const FRPGItemMoveRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(Request.RequestId, Descriptor, Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::Swap(
	const FRPGItemSwapRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(Request.RequestId, Descriptor, Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::Split(const FRPGItemSplitRequest& Request, FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion) return false;
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(Request.RequestId, Descriptor, Completion)) return true;
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::TransferStack(
	const FRPGItemStackTransferRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(Request.RequestId, Descriptor, Completion))
	{
		return true;
	}
	return Submit(Planner.Prepare(Request), MoveTemp(Completion));
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
	const FString EffectScope = FirstCommitSink.SupportsScopedConsumeRecovery()
		? TEXT("|effectScope=") + ConsumeEffectScope.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor))
	{
		Descriptor.CommandFingerprint += EffectScope;
		if (TryCompleteFromCache(Request.RequestId, Descriptor, Completion)) return true;
	}
	auto Plan = Planner.Prepare(Request);
	if (Plan.IsReady()) Plan.CommitRequest.CommandFingerprint += EffectScope;
	return Submit(MoveTemp(Plan), MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::ClaimMail(
	const FRPGItemClaimMailRequest& Request,
	FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion)
	{
		return false;
	}
	FRPGItemCommandDescriptor Descriptor;
	if (FRPGItemActionCommandPlanner::TryDescribe(Request, Descriptor) &&
		TryCompleteFromCache(Request.RequestId, Descriptor, Completion))
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
	if (const auto* Frozen = UncertainInventoryRequests.Find(RequestId))
	{
		FRPGItemAsyncCommandOutcome Outcome;
		const bool bSame = Frozen->Operation == Descriptor.Operation && Frozen->CommandFingerprint == Descriptor.CommandFingerprint;
		Outcome.Stage = bSame ? ERPGItemAsyncCommandStage::Uncertain : ERPGItemAsyncCommandStage::PlanRejected;
		Outcome.PlanFailureStatus = ERPGItemTransactionStatus::IdempotencyConflict;
		Completion(MoveTemp(Outcome));
		return true; // only explicit owner recovery may replay the frozen plan
	}
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
	if (HasUncertainInventoryCommands() || IsInventoryPersistenceBlocked())
	{
		FRPGItemAsyncCommandOutcome Outcome;
		Outcome.Stage = ERPGItemAsyncCommandStage::PlanRejected;
		Outcome.PlanFailureStatus = ERPGItemTransactionStatus::Forbidden;
		Completion(MoveTemp(Outcome));
		return false;
	}

	const FRPGItemRepositoryCommitRequest Request =
		MoveTemp(Plan.CommitRequest);
	if (Request.Operation == TEXT("Item.Consume") && FirstCommitSink.SupportsScopedConsumeRecovery())
	{
		FString Error;
		if (!FirstCommitSink.PrepareConsume(Request, Error))
		{
			FRPGItemAsyncCommandOutcome Outcome;
			Outcome.Stage = ERPGItemAsyncCommandStage::PreparationRejected;
			Outcome.Error = MoveTemp(Error);
			Completion(MoveTemp(Outcome));
			return false; // No item commit without a prepared effect and original avatar.
		}
	}
	return DispatchFrozen(Request, MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::FindUncertainInventoryCommand(
	const FGuid& RequestId, const FRPGItemOwnerRef& Owner, FName& Operation) const
{
	const auto* Frozen = UncertainInventoryRequests.Find(RequestId);
	if (!Frozen || Frozen->Actor != Owner) return false;
	Operation = Frozen->Operation;
	return true;
}

bool FRPGItemAsyncCommandOrchestrator::RetryInventoryCommand(
	const FGuid& RequestId, const FRPGItemOwnerRef& Owner, FRPGItemAsyncCommandCompletion Completion)
{
	if (!Completion) return false;
	FName Operation;
	if (!FindUncertainInventoryCommand(RequestId, Owner, Operation) || InventoryRetriesInFlight.Contains(RequestId))
	{
		FRPGItemAsyncCommandOutcome Outcome;
		Outcome.PlanFailureStatus = ERPGItemTransactionStatus::Forbidden;
		Completion(MoveTemp(Outcome));
		return false;
	}
	const auto Frozen = UncertainInventoryRequests.FindChecked(RequestId); // survives synchronous callbacks/map changes
	InventoryRetriesInFlight.Add(RequestId);
	return DispatchFrozen(Frozen, MoveTemp(Completion));
}

bool FRPGItemAsyncCommandOrchestrator::DispatchFrozen(
	const FRPGItemRepositoryCommitRequest& Request, FRPGItemAsyncCommandCompletion Completion)
{
	if (IsInventoryPersistenceBlocked() || (InventoryJournal && FRPGInventoryCommandJournal::Supports(Request.Operation) && !InventoryJournal->Persist(Request)))
	{
		bPersistenceFailure = true;
		if (Request.Operation == TEXT("Item.Consume") && !UncertainInventoryRequests.Contains(Request.RequestId))
			FirstCommitSink.DiscardConsume(Request.RequestId);
		InventoryRetriesInFlight.Remove(Request.RequestId);
		FRPGItemAsyncCommandOutcome Outcome;
		Outcome.Stage = ERPGItemAsyncCommandStage::PersistenceRejected;
		Outcome.Error = TEXT("The original item command could not be durably preserved. No HTTP request was sent.");
		Completion(MoveTemp(Outcome));
		return false;
	}
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
			Self->HandleCommit(Request,
				[Self, Request, Completion = MoveTemp(Completion)](FRPGItemAsyncCommandOutcome Outcome) mutable
				{
					Self->InventoryRetriesInFlight.Remove(Request.RequestId);
					const auto Status = Outcome.BackendResult.Status;
					const bool bAmbiguous = Outcome.BackendResult.bCommitOutcomeUncertain || Status == ERPGItemBackendStatus::TransportError ||
						Status == ERPGItemBackendStatus::ServerError || Status == ERPGItemBackendStatus::Unavailable ||
						Status == ERPGItemBackendStatus::ProtocolError ||
						Outcome.Stage == ERPGItemAsyncCommandStage::ProtocolRejected ||
						Outcome.Stage == ERPGItemAsyncCommandStage::ProjectionRejected ||
						Outcome.Stage == ERPGItemAsyncCommandStage::PostCommitRejected;
					const bool bRecoverable = Self->IsRecoverableOperation(Request.Operation);
					const bool bKeepPending = !Outcome.WasSuccessful() && bRecoverable &&
						(bAmbiguous || Self->UncertainInventoryRequests.Contains(Request.RequestId));
					if (FRPGInventoryCommandJournal::Supports(Request.Operation) && !bKeepPending && Self->InventoryJournal && !Self->InventoryJournal->ForgetVerified(Request))
					{
						Self->bPersistenceFailure = true;
						Self->UncertainInventoryRequests.Add(Request.RequestId, Request);
						Outcome.Stage = ERPGItemAsyncCommandStage::Uncertain;
						Outcome.Error = TEXT("The item outcome was observed but recovery storage could not be finalized.");
					}
					else if (Outcome.WasSuccessful()) Self->UncertainInventoryRequests.Remove(Request.RequestId);
					else if (bKeepPending)
					{
						Self->UncertainInventoryRequests.Add(Request.RequestId, Request);
						Outcome.Stage = ERPGItemAsyncCommandStage::Uncertain;
					}
					if (Request.Operation == TEXT("Item.Consume") && !bKeepPending)
						Self->FirstCommitSink.DiscardConsume(Request.RequestId);
					Completion(MoveTemp(Outcome));
				}, MoveTemp(Result));
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
	// Every planned item mutation, including Consume, must match before any
	// projection update or first-commit gameplay effect is allowed.
	{
		bool bMatches = Result.Records.Num() == Request.Mutations.Num() && Result.AffectedQuantity == Request.AffectedQuantity;
		TSet<FGuid> Matched;
		for (const auto& Mutation : Request.Mutations)
		{
			const auto* Actual = Result.Records.FindByPredicate([&Mutation](const auto& Record)
				{ return Record.GetItemId() == Mutation.NewRecord.GetItemId(); });
			if (!Actual || Matched.Contains(Actual->GetItemId()) || Mutation.ExpectedRevision == MAX_int64)
			{ bMatches = false; break; }
			const auto Expected = Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1);
			if (!FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Expected, Actual, 0)) { bMatches = false; break; }
			Matched.Add(Actual->GetItemId());
		}
		if (!bMatches)
		{
			Outcome.Stage = ERPGItemAsyncCommandStage::ProtocolRejected;
			Outcome.Error = TEXT("The item receipt records differ from the planned item mutations.");
			Completion(MoveTemp(Outcome));
			return;
		}
	}

	FString SinkError;
	if (!CommitSink.ApplyCommit(Request.Actor, Result, SinkError))
	{
		Outcome.Stage = ERPGItemAsyncCommandStage::ProjectionRejected;
		Outcome.Error = MoveTemp(SinkError);
		Completion(MoveTemp(Outcome));
		return;
	}

	if ((Result.Status == ERPGItemBackendStatus::Succeeded ||
		(Request.Operation == TEXT("Item.Consume") && FirstCommitSink.SupportsScopedConsumeRecovery())) &&
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

bool FRPGItemAsyncCommandOrchestrator::Sort(const FGuid& RequestId, const FRPGItemOwnerRef& Owner,
	EItemCategory Category, FRPGItemAsyncCommandCompletion Completion)
{ return Submit(Planner.PrepareSort(RequestId, Owner, Category), MoveTemp(Completion)); }
