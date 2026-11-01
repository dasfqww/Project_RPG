#pragma once

#include "CoreMinimal.h"

/** An immutable server-observed report. Credentials never enter the journal. */
struct PROJECT_RPG_API FRPGDungeonDisconnectionRecord
{
	FString CharacterId;
	FString SteamId;
	int64 ConnectionGeneration = 0;
	// Zero only for legacy v1 reports whose original observation is unknown.
	FDateTime ObservedAtUtc;
	FString Body;

	FString Key() const;
	bool EncodeBody(const FString& SessionId, FString& OutBody) const;
	bool SameAs(const FRPGDungeonDisconnectionRecord& Other) const;
};

/** Single assigned-server outbox: flush before HTTP, recover exact bodies after restart. */
class PROJECT_RPG_API FRPGDungeonDisconnectionJournal
{
public:
	FRPGDungeonDisconnectionJournal(FString InDirectory, FString InBackendUrl,
		FString InServerId, FString InSessionId);
	bool Restore();
	bool RecoverForRetry();
	bool Persist(const FRPGDungeonDisconnectionRecord& Record);
	bool Resolve(const FString& Key);
	bool IsHealthy() const { return bHealthy; }
	bool MatchesAssignment(const FString& Backend, const FString& Server, const FString& Session) const;
	const FString& GetDirectory() const { return Directory; }
	const TMap<FString, FRPGDungeonDisconnectionRecord>& GetPending() const { return Pending; }

	static bool IsAcceptedResponse(const FString& Json, int32 HttpStatus);
	static bool IsTerminalRejection(const FString& Json, int32 HttpStatus);

private:
	bool Validate(const FRPGDungeonDisconnectionRecord& Record) const;
	bool Read(const FString& FileName, FRPGDungeonDisconnectionRecord& OutRecord) const;
	FString PathFor(const FString& Key) const;
	FString Directory;
	FString BackendUrl;
	FString ServerId;
	FString SessionId;
	TMap<FString, FRPGDungeonDisconnectionRecord> Pending;
	bool bHealthy = false;
};
