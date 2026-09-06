#pragma once

#include "CoreMinimal.h"
#include "Security/RPGSecurityTypes.h"

/** One immutable event queued by an authoritative dedicated server. */
struct PROJECT_RPG_API FRPGSecurityTelemetryEvent
{
	FGuid EventId;
	FString CharacterId;
	FString SteamId;
	ERPGSecurityViolationType Type =
		ERPGSecurityViolationType::MovementSpeed;
	ERPGSecurityViolationSeverity Severity =
		ERPGSecurityViolationSeverity::Low;
	double Score = 0.0;
	double RiskAfter = 0.0;
	double ServerTimeSeconds = 0.0;
	FString Detail;
};

/**
 * Bounded single-flight queue. Reserved entries cannot be evicted while an
 * HTTP request owns them, and only an acknowledged batch is removed.
 */
class PROJECT_RPG_API FRPGSecurityTelemetryQueue
{
public:
	explicit FRPGSecurityTelemetryQueue(int32 InCapacity = 512);

	void SetCapacity(int32 InCapacity);
	bool Enqueue(FRPGSecurityTelemetryEvent Event);
	TArray<FRPGSecurityTelemetryEvent> BeginBatch(int32 MaximumBatchSize);
	void CompleteBatch(bool bRemoveReservedEvents);
	void Reset();

	int32 Num() const { return Events.Num(); }
	int32 GetReservedCount() const { return ReservedEventIds.Num(); }
	int64 GetDroppedCount() const { return DroppedCount; }
	bool HasBatchInFlight() const { return !ReservedEventIds.IsEmpty(); }
	TConstArrayView<FRPGSecurityTelemetryEvent> GetEvents() const
	{
		return Events;
	}
	TArray<FRPGSecurityTelemetryEvent> GetReservedEvents() const;

private:
	bool IsReserved(const FGuid& EventId) const;
	int32 FindOldestEvictableIndex() const;

	int32 Capacity = 512;
	int64 DroppedCount = 0;
	TArray<FRPGSecurityTelemetryEvent> Events;
	TArray<FGuid> ReservedEventIds;
};

struct PROJECT_RPG_API FRPGSecurityTelemetryJsonCodec
{
	static bool SerializeBatch(
		const FString& DungeonSessionId,
		TConstArrayView<FRPGSecurityTelemetryEvent> Events,
		FString& OutJson);
	static bool DeserializeBatch(
		const FString& Json,
		FString& OutDungeonSessionId,
		TArray<FRPGSecurityTelemetryEvent>& OutEvents);

	/** A 2xx response is an acknowledgement only when every event is accounted for. */
	static bool IsBatchAcknowledgementValid(
		const FString& Json,
		int32 ExpectedEventCount);
};
