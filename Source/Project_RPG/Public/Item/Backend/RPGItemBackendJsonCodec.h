#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"

/** Pure JSON boundary between Unreal item records and the backend V2 contract. */
class PROJECT_RPG_API FRPGItemBackendJsonCodec
{
public:
	static bool SerializeCommitRequest(
		const FRPGItemRepositoryCommitRequest& Request,
		FString& OutJson,
		FString* OutError = nullptr);

	static bool DeserializeCommitResponse(
		const FString& Json,
		FRPGItemBackendCommitResult& OutResult,
		FString* OutError = nullptr);

	static bool DeserializeLoadResponse(
		const FString& Json,
		TArray<FRPGItemRecord>& OutRecords,
		FString* OutError = nullptr);
};
