#pragma once

#include "CoreMinimal.h"

/** An actual, original-peer loss while its consume result is unresolved. No ticket or credentials. */
struct PROJECT_RPG_API FRPGDungeonConsumeLossRecord
{
	FString RequestId, TicketHash, ExpectedSteamId;
	FDateTime ObservedAtUtc;
	bool SameAs(const FRPGDungeonConsumeLossRecord& Other) const;
};

/** Single assigned-process journal. Missing observations are never synthesized on restart. */
class PROJECT_RPG_API FRPGDungeonConsumeJournal
{
public:
	FRPGDungeonConsumeJournal(FString InDirectory, FString InBackendUrl, FString InServerId, FString InSessionId);
	bool Restore();
	/** Retry storage recovery without forgetting any previously durable observation. */
	bool RecoverForRetry();
	bool Persist(const FRPGDungeonConsumeLossRecord& Record);
	bool Resolve(const FString& RequestId);
	bool IsHealthy() const { return bHealthy; }
	bool MatchesAssignment(const FString& Backend, const FString& Server, const FString& Session) const;
	const TMap<FString, FRPGDungeonConsumeLossRecord>& GetPending() const { return Pending; }

private:
	bool Validate(const FRPGDungeonConsumeLossRecord& Record) const;
	bool Read(const FString& FileName, FRPGDungeonConsumeLossRecord& Record) const;
	FString PathFor(const FString& RequestId) const;
	FString Directory, BackendUrl, ServerId, SessionId;
	TMap<FString, FRPGDungeonConsumeLossRecord> Pending;
	bool bHealthy = false;
};
