#pragma once

#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
#include "Item/Persistence/RPGItemRecord.h"

enum class ERPGDungeonRewardSnapshotStatus : uint8 { Ready, Pending, Failed };

/** Server-only response; never populated by a player RPC. */
struct PROJECT_RPG_API FRPGDungeonRewardSnapshotResult
{
	ERPGDungeonRewardSnapshotStatus Status = ERPGDungeonRewardSnapshotStatus::Failed;
	int32 HttpStatus = 0;
	FString DungeonSessionId;
	FString CharacterId;
	FRPGEconomyWalletResult Wallet;
	TArray<FRPGItemRecord> Items;
	/** Actual immutable item receipt quantity, never the sum of optional candidates. */
	TOptional<int32> DeliveredItemQuantity;
	FString DeliveryRewardVersion;
};

class PROJECT_RPG_API FRPGDungeonRewardSnapshotCodec
{
public:
	static bool Decode(const FString& Json, int32 HttpStatus, const FString& ExpectedSessionId,
		const FString& ExpectedCharacterId, FRPGDungeonRewardSnapshotResult& OutResult);
};
