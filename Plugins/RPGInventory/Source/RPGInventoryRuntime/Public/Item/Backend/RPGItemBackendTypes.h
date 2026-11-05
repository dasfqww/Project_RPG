#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRepository.h"

enum class ERPGItemBackendStatus : uint8
{
	Succeeded,
	AlreadyApplied,
	Unavailable,
	InvalidRequest,
	Unauthorized,
	Forbidden,
	NotFound,
	IdempotencyConflict,
	RevisionConflict,
	LocationConflict,
	ValidationFailed,
	TransportError,
	ProtocolError,
	ServerError
};

struct RPGINVENTORYRUNTIME_API FRPGItemBackendHttpRequest
{
	FString Verb;
	FString RelativePath;
	FString Body;
};

struct RPGINVENTORYRUNTIME_API FRPGItemBackendHttpResponse
{
	bool bTransportSuccessful = false;
	int32 StatusCode = 0;
	FString Body;
};

struct RPGINVENTORYRUNTIME_API FRPGItemBackendLoadResult
{
	ERPGItemBackendStatus Status = ERPGItemBackendStatus::Unavailable;
	int32 HttpStatusCode = 0;
	TArray<FRPGItemRecord> Records;
	FString Error;

	bool WasSuccessful() const
	{
		return Status == ERPGItemBackendStatus::Succeeded;
	}
};

struct RPGINVENTORYRUNTIME_API FRPGItemBackendCommitResult
{
	ERPGItemBackendStatus Status = ERPGItemBackendStatus::Unavailable;
	/** An earlier wire attempt may have committed before its response was lost. */
	bool bCommitOutcomeUncertain = false;
	int32 HttpStatusCode = 0;
	FGuid RequestId;
	FName Operation;
	FString CommandFingerprint;
	FRPGItemOwnerRef Actor;
	int32 AffectedQuantity = 0;
	TArray<FRPGItemRecord> Records;
	FString Error;

	bool WasSuccessful() const
	{
		return Status == ERPGItemBackendStatus::Succeeded ||
			Status == ERPGItemBackendStatus::AlreadyApplied;
	}
};

using FRPGItemBackendHttpCompletion =
	TFunction<void(FRPGItemBackendHttpResponse)>;
using FRPGItemBackendLoadCompletion =
	TFunction<void(FRPGItemBackendLoadResult)>;
using FRPGItemBackendCommitCompletion =
	TFunction<void(FRPGItemBackendCommitResult)>;
