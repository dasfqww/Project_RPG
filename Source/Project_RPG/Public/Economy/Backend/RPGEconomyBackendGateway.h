#pragma once

#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendTransport.h"

class PROJECT_RPG_API FRPGEconomyBackendGateway
{
public:
	FRPGEconomyBackendGateway(
		TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> InTransport,
		int32 InMaximumAttempts = 2);

	void LoadWallet(
		const FString& CharacterId,
		const FString& DungeonSessionId,
		FRPGEconomyWalletCompletion Completion) const;

	void Commit(
		const FRPGEconomyTransactionRequest& Request,
		FRPGEconomyCommitCompletion Completion) const;

private:
	TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> Transport;
	int32 MaximumAttempts = 2;
};
