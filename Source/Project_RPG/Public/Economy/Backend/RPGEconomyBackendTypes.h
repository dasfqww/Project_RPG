#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"

enum class ERPGEconomyBackendStatus : uint8
{
	Succeeded,
	AlreadyApplied,
	Unavailable,
	InvalidRequest,
	Unauthorized,
	Forbidden,
	NotFound,
	IdempotencyConflict,
	CurrencyDisabled,
	InsufficientBalance,
	BalanceLimitExceeded,
	TransportError,
	ProtocolError,
	ServerError
};

struct PROJECT_RPG_API FRPGEconomyTransactionRequest
{
	FGuid RequestId;
	FString CharacterId;
	FString DungeonSessionId;
	FName Operation;
	FString CommandFingerprint;
	FString Reason;
	TArray<FRPGCurrencyChange> Changes;
};

struct PROJECT_RPG_API FRPGEconomyHttpRequest
{
	FString Verb;
	FString RelativePath;
	FString Body;
};

struct PROJECT_RPG_API FRPGEconomyHttpResponse
{
	bool bTransportSuccessful = false;
	int32 StatusCode = 0;
	FString Body;
};

struct PROJECT_RPG_API FRPGEconomyWalletResult
{
	ERPGEconomyBackendStatus Status = ERPGEconomyBackendStatus::Unavailable;
	int32 HttpStatusCode = 0;
	FString CharacterId;
	FString RosterId;
	FString AccountId;
	TArray<FRPGCurrencyBalance> Balances;
	FString Error;

	bool WasSuccessful() const
	{
		return Status == ERPGEconomyBackendStatus::Succeeded;
	}
};

struct PROJECT_RPG_API FRPGEconomyCommitResult
{
	ERPGEconomyBackendStatus Status = ERPGEconomyBackendStatus::Unavailable;
	int32 HttpStatusCode = 0;
	FGuid RequestId;
	FString CharacterId;
	FName Operation;
	FString CommandFingerprint;
	FString Reason;
	TArray<FRPGCurrencyChangeResult> Changes;
	FString Error;

	bool WasSuccessful() const
	{
		return Status == ERPGEconomyBackendStatus::Succeeded ||
			Status == ERPGEconomyBackendStatus::AlreadyApplied;
	}
};

using FRPGEconomyHttpCompletion =
	TFunction<void(FRPGEconomyHttpResponse)>;
using FRPGEconomyWalletCompletion =
	TFunction<void(FRPGEconomyWalletResult)>;
using FRPGEconomyCommitCompletion =
	TFunction<void(FRPGEconomyCommitResult)>;
