#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTransport.h"

/**
 * Asynchronous application gateway for the backend Item V2 API.
 *
 * Transient commit retries reuse the exact serialized body and RequestId, so a
 * lost response cannot apply the command twice.
 */
class RPGINVENTORYRUNTIME_API FRPGItemBackendGateway
{
public:
	FRPGItemBackendGateway(
		TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> InTransport,
		int32 InMaximumAttempts = 2);

	void LoadItems(
		const FRPGItemOwnerRef& Owner,
		bool bIncludeTerminal,
		int32 Limit,
		FRPGItemBackendLoadCompletion Completion) const;

	void Commit(
		const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) const;

	/** Prepare the immutable drop origin, then consume it using an empty-body POST. */
	void CommitPickup(const FString& ServerId, const FGuid& SessionId, const FGuid& DropId,
		const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion,
		TFunction<bool()> CanContinue = {}) const;

	/** GET-only recovery of v2 journals; an unconsumed origin never authorizes replay. */
	void LoadPickupReceipt(const FString& ServerId, const FGuid& SessionId, const FGuid& DropId,
		const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion,
		TFunction<bool()> CanContinue = {}) const;

	/** Read-only reconciliation; 404 never causes a new commit. */
	void LoadCommitReceipt(
		const FRPGItemRepositoryCommitRequest& ExpectedRequest,
		FRPGItemBackendCommitCompletion Completion) const;

private:
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport;
	int32 MaximumAttempts = 2;
};
