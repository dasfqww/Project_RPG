#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTransport.h"

/**
 * Asynchronous application gateway for the backend Item V2 API.
 *
 * Transient commit retries reuse the exact serialized body and RequestId, so a
 * lost response cannot apply the command twice.
 */
class PROJECT_RPG_API FRPGItemBackendGateway
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

private:
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport;
	int32 MaximumAttempts = 2;
};
