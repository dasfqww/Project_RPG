#include "Component/RPGPickupGrantComponent.h"

#include "Async/Async.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/World.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Transaction/RPGPickupGrantReceiptPolicy.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGPickupGrantComponent)

URPGPickupGrantComponent::URPGPickupGrantComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool URPGPickupGrantComponent::BeginGrant(
	const FRPGItemRepositoryCommitRequest& Request,
	URPGItemCommandComponent& Commands,
	TSharedRef<IRPGItemAsyncCommitter> InCommitter, const bool bPreparedPickup)
{
	ARPGPickUpBase* Pickup = Cast<ARPGPickUpBase>(GetOwner());
	if (bPending || !Pickup || !Pickup->HasAuthority() ||
		Request.Mutations.Num() != 1 || !Commands.IsAuthenticatedItemOwner(Request.Actor) ||
		!Pickup->TryClaimPickup())
	{
		return false;
	}
	FrozenRequest = Request;
	ClaimantCommands = &Commands;
	Committer = MoveTemp(InCommitter);
	AcceptedReceipt = {};
	RetryCount = 0;
	bHasReceipt = false;
	bRequestedRefresh = false;
	bPreserveReservation = bPreparedPickup;
	bPending = true;
	if (!Commands.TrackPickupGrant(FrozenRequest))
	{
		// No HTTP/journal write has happened. Undo only this failed local claim.
		bPending = false;
		ClaimantCommands.Reset();
		Committer.Reset();
		FrozenRequest = {};
		bPreserveReservation = false;
		Pickup->ReleasePickupClaim();
		return false;
	}
	SendCommit();
	return true;
}

void URPGPickupGrantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(RetryTimer);
	if (bPending)
	{
		if (URPGItemCommandComponent* Commands = ClaimantCommands.Get())
		{
			Commands->CompletePickupGrant(FrozenRequest, ERPGItemCommandResultCode::ServerStateError);
		}
	}
	bPending = false;
	ClaimantCommands.Reset();
	Committer.Reset();
	Super::EndPlay(EndPlayReason);
}

void URPGPickupGrantComponent::SendCommit()
{
	if (!bPending) return;
	if (bHasReceipt)
	{
		ApplyReceipt();
		return;
	}
	const URPGItemCommandComponent* Commands = ClaimantCommands.Get();
	if (!Commands || !Commands->IsAuthenticatedItemOwner(FrozenRequest.Actor))
	{
		// An earlier submission may already exist in the DB. Stop new writes,
		// but retain the frozen command and claimed pickup until reconciliation
		// or world teardown; do not expose it to a second claimant.
		if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
		return;
	}
	const TSharedPtr<IRPGItemAsyncCommitter> CurrentCommitter = Committer;
	if (!CurrentCommitter)
	{
		ScheduleRetry();
		return;
	}
	const TWeakObjectPtr<URPGPickupGrantComponent> WeakThis(this);
	CurrentCommitter->Commit(FrozenRequest,
		[WeakThis](FRPGItemBackendCommitResult Result) mutable
		{
			if (IsInGameThread())
			{
				if (URPGPickupGrantComponent* Self = WeakThis.Get()) Self->HandleReceipt(MoveTemp(Result));
				return;
			}
			AsyncTask(ENamedThreads::GameThread,
				[WeakThis, Result = MoveTemp(Result)]() mutable
				{
					if (URPGPickupGrantComponent* Self = WeakThis.Get()) Self->HandleReceipt(MoveTemp(Result));
				});
		});
}

void URPGPickupGrantComponent::HandleReceipt(FRPGItemBackendCommitResult Result)
{
	if (!bPending) return;
	switch (FRPGPickupGrantReceiptPolicy::Evaluate(FrozenRequest, Result, bPreserveReservation))
	{
	case ERPGPickupGrantReceiptDecision::ReleasePickup:
		AcceptedReceipt = MoveTemp(Result);
		Finish(false);
		break;
	case ERPGPickupGrantReceiptDecision::ApplyReceipt:
		AcceptedReceipt = MoveTemp(Result);
		bHasReceipt = true;
		ApplyReceipt();
		break;
	default:
		ScheduleRetry();
		break;
	}
}

void URPGPickupGrantComponent::ApplyReceipt()
{
	URPGItemCommandComponent* Commands = ClaimantCommands.Get();
	// Disconnected/switched characters reload the durable record on their next admission.
	if (!Commands || !Commands->IsAuthenticatedItemOwner(FrozenRequest.Actor))
	{
		Finish(true);
		return;
	}
	FString Error;
	if (Commands->ApplyCommitAndReconcile(FrozenRequest.Actor, AcceptedReceipt, Error))
	{
		Finish(true);
		return;
	}
	URPGInventoryProjectionComponent* Projection = Commands->GetOwner()
		? Commands->GetOwner()->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	const IRPGItemRecordSource* Records = Projection ? Projection->GetAuthoritativeRecordSource() : nullptr;
	FRPGItemRecord Current;
	FRPGInventoryProjectionEntry CurrentEntry;
	if (Records && Records->Find(AcceptedReceipt.Records[0].GetItemId(), Current) &&
		Current.GetOwner() == FrozenRequest.Actor &&
		Current.GetRevision() > AcceptedReceipt.Records[0].GetRevision() &&
		(!Current.IsActive() ||
			(Projection->FindProjectedItem(Current.GetItemId(), CurrentEntry) &&
				CurrentEntry.GetRevision() == Current.GetRevision())))
	{
		Finish(true);
		return;
	}
	if (Projection && (!bRequestedRefresh ||
		Projection->GetLoadState() == ERPGInventoryProjectionLoadState::Failed))
	{
		bRequestedRefresh = Projection->LoadAuthenticatedCharacterItems();
	}
	ScheduleRetry();
}

void URPGPickupGrantComponent::ScheduleRetry()
{
	if (!bPending || !GetWorld()) return;
	RetryCount = FMath::Min(RetryCount + 1, 5);
	const float Delay = bHasReceipt ? 2.f
		: FMath::Min(30.f, 2.f * static_cast<float>(1 << FMath::Min(RetryCount - 1, 4)));
	GetWorld()->GetTimerManager().SetTimer(RetryTimer, this, &ThisClass::SendCommit, Delay, false);
}

void URPGPickupGrantComponent::Finish(const bool bCommitted)
{
	if (!bPending) return;
	bPending = false;
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(RetryTimer);
	const TWeakObjectPtr<URPGItemCommandComponent> Commands = ClaimantCommands;
	const FRPGItemRepositoryCommitRequest Request = FrozenRequest;
	const ERPGItemCommandResultCode Code = bCommitted
			? (AcceptedReceipt.Status == ERPGItemBackendStatus::AlreadyApplied
				? ERPGItemCommandResultCode::AlreadyApplied : ERPGItemCommandResultCode::Succeeded)
			: (AcceptedReceipt.Status == ERPGItemBackendStatus::LocationConflict ||
				AcceptedReceipt.Status == ERPGItemBackendStatus::RevisionConflict
				? ERPGItemCommandResultCode::Conflict : ERPGItemCommandResultCode::BackendRejected);
	ClaimantCommands.Reset();
	Committer.Reset();
	if (ARPGPickUpBase* Pickup = Cast<ARPGPickUpBase>(GetOwner()))
	{
		if (bCommitted) Pickup->PickedUp();
		else Pickup->ReleasePickupClaim();
	}
	if (Commands.IsValid()) Commands->CompletePickupGrant(Request, Code);
}
