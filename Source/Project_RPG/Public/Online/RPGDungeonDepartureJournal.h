#pragma once

#include "CoreMinimal.h"

/** An untrusted recovery hint, never a credential, membership or server receipt. */
struct PROJECT_RPG_API FRPGDungeonDepartureCommand
{
	FString BackendUrl;
	FString SteamId;
	FString CharacterId;
	FString SessionId;
	FString RequestId;
	int64 LobbyRevision = 0;
	bool bCancel = false;
	bool IsValid() const;
	bool SameAs(const FRPGDungeonDepartureCommand& Other) const;
};

enum class ERPGDepartureJournalLoad : uint8 { Missing, Found, Error };

/** One immutable command per backend/account/character. Flush before dispatch, exact-match removal. */
class PROJECT_RPG_API FRPGDungeonDepartureJournal
{
public:
	FRPGDungeonDepartureJournal(const FString& Directory, const FString& BackendUrl,
		const FString& SteamId, const FString& CharacterId);
	ERPGDepartureJournalLoad Load(FRPGDungeonDepartureCommand& OutCommand);
	bool Persist(const FRPGDungeonDepartureCommand& Command);
	bool Resolve(const FRPGDungeonDepartureCommand& Command);
	const FString& GetPath() const { return Path; }

private:
	ERPGDepartureJournalLoad LoadLocked(FRPGDungeonDepartureCommand& OutCommand);
	bool Read(const FString& FilePath, FRPGDungeonDepartureCommand& OutCommand) const;
	bool MatchesScope(const FRPGDungeonDepartureCommand& Command) const;
	FString Directory;
	FString BackendUrl;
	FString SteamId;
	FString CharacterId;
	FString Path;
	FString LockName;
};
