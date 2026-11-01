#pragma once

#include "CoreMinimal.h"

/** Server-only result of a consumed, one-use backend join ticket. */
struct PROJECT_RPG_API FRPGJoinTicketAdmission
{
	FString DungeonSessionId;
	FString CharacterId;
	FString SteamId;
	int64 ConnectionGeneration = 0;

	/** Requires the exact numeric generation; never rounds JSON decimals/doubles. */
	static bool Decode(const FString& Json, FRPGJoinTicketAdmission& OutAdmission);
};

/** Historical evidence only. Never use this envelope to complete admission or activate an identity. */
struct PROJECT_RPG_API FRPGJoinTicketConsumptionEvidence
{
	FRPGJoinTicketAdmission Consumption;
	FString ServerId, ConsumptionRequestId;
	FDateTime ConsumedAtUtc;
	static bool Decode(const FString& Json, FRPGJoinTicketConsumptionEvidence& OutEvidence);
};

/** Cancellation proof for one exact intent/hash/peer, never an admission or disconnect. */
struct PROJECT_RPG_API FRPGJoinTicketConsumptionCancellation
{
	FString DungeonSessionId, ServerId, ConsumptionRequestId, TicketHash, ExpectedSteamId;
	FDateTime CancelledAtUtc;
	static bool Decode(const FString& Json, FRPGJoinTicketConsumptionCancellation& OutCancellation);
};

/** Backend confirmation is authoritative only for an exact HTTP 200/true response. */
struct PROJECT_RPG_API FRPGDungeonAdmissionConfirmation
{
	static bool Decode(const FString& Json, int32 HttpStatus);
};

/** Shared, exact JSON body for server admission/disconnection requests. */
struct PROJECT_RPG_API FRPGDungeonConnectionRequest
{
	static bool Encode(const FString& DungeonSessionId, const FString& CharacterId,
		const FString& SteamId, int64 ConnectionGeneration, FString& OutBody);

	/** Freeze the server's socket-loss observation, not the later HTTP send time. */
	static bool EncodeDisconnection(const FString& DungeonSessionId, const FString& CharacterId,
		const FString& SteamId, int64 ConnectionGeneration, FDateTime ObservedAtUtc, FString& OutBody);
};

/** Bounded in-process retries; definitive authorization/state rejection stops. */
struct PROJECT_RPG_API FRPGDungeonDisconnectionRetryPolicy
{
	static float DelaySeconds(int32 CompletedAttempt, int32 HttpStatus);
};
