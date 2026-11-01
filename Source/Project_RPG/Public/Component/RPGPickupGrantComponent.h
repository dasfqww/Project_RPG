#pragma once

#include "Components/ActorComponent.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "RPGPickupGrantComponent.generated.h"

class URPGItemCommandComponent;
class IRPGItemAsyncCommitter;

/** Server-side lifetime of one claimed pickup and its immutable grant request. */
UCLASS()
class PROJECT_RPG_API URPGPickupGrantComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGPickupGrantComponent();
	bool BeginGrant(const FRPGItemRepositoryCommitRequest& Request,
		URPGItemCommandComponent& Commands,
		TSharedRef<IRPGItemAsyncCommitter> InCommitter, bool bPreparedPickup = false);
	bool IsPending() const { return bPending; }
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SendCommit();
	void HandleReceipt(FRPGItemBackendCommitResult Result);
	void ApplyReceipt();
	void ScheduleRetry();
	void Finish(bool bCommitted);

	FRPGItemRepositoryCommitRequest FrozenRequest;
	FRPGItemBackendCommitResult AcceptedReceipt;
	TWeakObjectPtr<URPGItemCommandComponent> ClaimantCommands;
	TSharedPtr<IRPGItemAsyncCommitter> Committer;
	FTimerHandle RetryTimer;
	int32 RetryCount = 0;
	bool bPending = false;
	bool bHasReceipt = false;
	bool bRequestedRefresh = false;
	bool bPreserveReservation = false;
};
