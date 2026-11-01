#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"

/**
 * Server-local write-ahead journal. One process owns one allocation directory.
 * Restoring it grants no write authority: missing receipts remain unresolved.
 * The volume/ACL must be owned by the dedicated server, never by game clients.
 */
class PROJECT_RPG_API FRPGPickupGrantJournal final
{
public:
	FRPGPickupGrantJournal(FString InDirectory, FString InServerId, FString InSessionId);
	bool Restore();
	bool Persist(const FRPGItemRepositoryCommitRequest& Request, const FGuid& DropId = {});
	bool Resolve(const FRPGItemBackendCommitResult& Receipt);
	bool IsHealthy() const { return bHealthy; }
	const TMap<FGuid, FRPGItemRepositoryCommitRequest>& GetPending() const { return Pending; }
	FGuid GetDropId(const FGuid& RequestId) const { return DropIds.FindRef(RequestId); }
	const FString& GetServerId() const { return ServerId; }
	const FString& GetSessionId() const { return SessionId; }

private:
	bool ReadEntry(const FString& FileName, FRPGItemRepositoryCommitRequest& OutRequest, FString& OutBody,
		FGuid& OutDropId) const;
	FString PathFor(const FGuid& RequestId) const;

	FString Directory;
	FString ServerId;
	FString SessionId;
	TMap<FGuid, FRPGItemRepositoryCommitRequest> Pending;
	TMap<FGuid, FString> FrozenBodies;
	TMap<FGuid, FGuid> DropIds;
	bool bHealthy = false;
};
