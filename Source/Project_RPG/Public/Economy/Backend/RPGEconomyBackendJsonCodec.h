#pragma once

#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"

class PROJECT_RPG_API FRPGEconomyBackendJsonCodec
{
public:
	static bool SerializeTransactionRequest(
		const FRPGEconomyTransactionRequest& Request,
		FString& OutJson,
		FString* OutError = nullptr);

	static bool DeserializeWalletResponse(
		const FString& Json,
		FRPGEconomyWalletResult& OutResult,
		FString* OutError = nullptr);

	static bool DeserializeCommitResponse(
		const FString& Json,
		FRPGEconomyCommitResult& OutResult,
		FString* OutError = nullptr);
};
