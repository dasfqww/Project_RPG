#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"

/** Server-owned allocation context, not a credential or a grant of item authority. */
struct RPGINVENTORYRUNTIME_API FRPGInventoryJournalScope
{
	FString BackendUrl, ServerId, SessionId, CharacterId, SteamId;
	bool IsValid() const;
};

/**
 * Server-only write-ahead store for bag commands, ClaimMail and Equip/Unequip. The dedicated
 * server must own the volume/ACL; files are never accepted from a game client.
 * Restore only supplies frozen plans. Current admission and backend CAS still apply.
 */
class RPGINVENTORYRUNTIME_API FRPGInventoryCommandJournal final
{
public:
	FRPGInventoryCommandJournal(const FString& RootDirectory, FRPGInventoryJournalScope InScope);
	bool Restore();
	bool Persist(const FRPGItemRepositoryCommitRequest& Request);
	/** Only a validated outcome may retire a pending entry; missing/failed retries are not evidence. */
	bool ForgetVerified(const FRPGItemRepositoryCommitRequest& Request);
	/** Irreversible for this exact allocation/identity, including when no command is pending. */
	bool Quarantine();
	bool IsHealthy() const { return bHealthy; }
	bool IsRevoked() const { return State.bRevoked; }
	bool MatchesIdentity(const FString& Session, const FString& Character, const FString& Steam) const
	{ return Scope.SessionId == Session && Scope.CharacterId == Character && Scope.SteamId == Steam; }
	const TMap<FGuid, FRPGItemRepositoryCommitRequest>& GetPending() const { return State.Requests; }
	const FString& GetPath() const { return Path; }
	static bool Supports(FName Operation);

private:
	struct FState
	{
		uint64 Generation = 0;
		bool bRevoked = false;
		TMap<FGuid, FRPGItemRepositoryCommitRequest> Requests;
		TMap<FGuid, FString> Bodies;
	};
	bool ValidRequest(const FRPGItemRepositoryCommitRequest& Request) const;
	bool Read(const FString& FilePath, FState& Out) const;
	bool LoadLocked();
	bool WriteLocked(FState Next);
	FString Directory, Path, LockName;
	FRPGInventoryJournalScope Scope;
	FState State;
	bool bHealthy = false;
};
