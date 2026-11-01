#pragma once

#include "CoreMinimal.h"
#include "Online/RPGDungeonAbandonment.h"

struct FRPGDungeonMembershipRecord
{
	FString CharacterId;
	FString SteamId;
	FString LeaseExpiresAt;
	// Optional presentation metadata; never admission/revocation evidence.
	FString CharacterName;
	FString DisconnectedAt;
	FString ReconnectExpiresAt;
	int64 ConnectionGeneration = 0;
	int64 DisconnectedGeneration = 0;
	bool bHasDisconnectedGeneration = false;
	bool bHasConnectionPresentation = false;
};

struct FRPGDungeonMembershipAbandonment
{
	FString SteamId;
	FRPGDungeonAbandonmentReceipt Receipt;
};

struct FRPGDungeonMembershipExpulsion
{
	FString CharacterId;
	FString SteamId;
	FString DisconnectedAt;
	FString ExpiredAt;
	FString ProcessedAt;
	bool bRewardExcluded = false;
};

/** Server-only read model. A member record is not an admission or gameplay grant. */
struct PROJECT_RPG_API FRPGDungeonMembershipSnapshot
{
	FString DungeonSessionId;
	FString ServerId;
	FString State;
	FString UpdatedAt;
	FString ExpiresAt;
	TArray<FRPGDungeonMembershipRecord> Members;
	TArray<FRPGDungeonMembershipAbandonment> Abandonments;
	TArray<FRPGDungeonMembershipExpulsion> DisconnectionExpulsions;
	FString ObservedAt;

	/** Missing members or failed reads never prove explicit abandonment. */
	bool IsExplicitlyAbandoned(const FString& ExpectedSessionId, const FString& ExpectedServerId,
		const FString& CharacterId, const FString& SteamId) const;
	/** Only an exact immutable server expiry receipt, never absence, proves disconnection expulsion. */
	bool IsDisconnectedExpelled(const FString& ExpectedSessionId, const FString& ExpectedServerId,
		const FString& CharacterId, const FString& SteamId) const;
};

class PROJECT_RPG_API FRPGDungeonMembershipSnapshotCodec
{
public:
	static bool Decode(const FString& Body, int32 HttpStatus, const FString& ExpectedSessionId,
		const FString& ExpectedServerId, FRPGDungeonMembershipSnapshot& OutSnapshot);
};

/** Native server transport result; no player RPC or Blueprint authority surface. */
struct FRPGDungeonMembershipSnapshotResult
{
	FRPGDungeonMembershipSnapshot Snapshot;
	int32 HttpStatus = 0;
	bool bSuccess = false;
};

/** Native server presentation cache, independent of the immutable evidence monitor. */
struct FRPGDungeonMembershipPresentation
{
	FRPGDungeonMembershipSnapshot Snapshot;
	uint64 Revision = 0;
	double ReceivedAtSeconds = 0.0;
	double RequestStartedAtSeconds = 0.0;
	bool bHasRequestClock = false;
	bool bStale = true;
};

/** Scope-local, monotonic opt-out evidence. Never infers revocation from absence/errors. */
class PROJECT_RPG_API FRPGDungeonMembershipMonitor
{
public:
	bool Configure(const FString& SessionId, const FString& ServerId);
	void Reset();
	bool BeginRead(uint64& OutReadSerial);
	bool FinishRead(uint64 ReadSerial, const FRPGDungeonMembershipSnapshotResult& Result);
	bool IsExplicitlyAbandoned(const FString& CharacterId, const FString& SteamId) const;
	bool IsDisconnectedExpelled(const FString& CharacterId, const FString& SteamId) const;
	const FString& GetSessionId() const { return SessionId; }
	const FString& GetServerId() const { return ServerId; }

private:
	FString SessionId;
	FString ServerId;
	TMap<FGuid, FString> AbandonedCharacters;
	TMap<FGuid, FString> ExpelledCharacters;
	uint64 NextReadSerial = 0;
	uint64 ActiveReadSerial = 0;
};
