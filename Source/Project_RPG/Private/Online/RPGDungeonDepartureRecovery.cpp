#include "Online/RPGDungeonSessionSubsystem.h"

#include "Misc/Paths.h"
#include "Online/RPGDungeonDepartureJournal.h"

FString URPGDungeonSessionSubsystem::GetDepartureRecoveryDirectory() const
{
	return DepartureRecoveryDirectory.IsEmpty()
		? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Online/DepartureCommands")) : DepartureRecoveryDirectory;
}

FRPGDungeonDepartureCommand URPGDungeonSessionSubsystem::GetPendingDepartureCommand() const
{
	return { PendingDepartureBackend, PendingDepartureSteam, PendingDepartureCharacter, PendingDepartureSession,
		PendingDepartureRequest, PendingDepartureRevision, bPendingDepartureCancel };
}

bool URPGDungeonSessionSubsystem::IsDepartureScopeCurrent() const
{
	return WebManager && WebManager->HasAccessToken() && PendingDepartureBackend.Equals(WebManager->GetApiUrl(), ESearchCase::CaseSensitive)
		&& PendingDepartureSteam == WebManager->GetAuthenticatedSteamId() && PendingDepartureCharacter == SelectedCharacterId;
}

bool URPGDungeonSessionSubsystem::RestoreDepartureCommand()
{
	// Selection before login is harmless, but never reads/restores an unauthenticated account's disk hint.
	if (!WebManager || !WebManager->HasAccessToken() || SelectedCharacterId.IsEmpty()) return true;
	if (HasUnresolvedDeparture() && !IsDepartureScopeCurrent())
	{
		bDepartureRecoveryBlocked = true; FailOperation(TEXT("departure_recovery_scope_mismatch")); return false;
	}
	FRPGDungeonDepartureJournal Journal(GetDepartureRecoveryDirectory(), WebManager->GetApiUrl(),
		WebManager->GetAuthenticatedSteamId(), SelectedCharacterId);
	FRPGDungeonDepartureCommand Restored;
	const auto Loaded = Journal.Load(Restored);
	if (Loaded == ERPGDepartureJournalLoad::Error
		|| (Loaded == ERPGDepartureJournalLoad::Found && HasUnresolvedDeparture() && !Restored.SameAs(GetPendingDepartureCommand())))
	{
		bDepartureRecoveryBlocked = true; FailOperation(TEXT("departure_recovery_read_failed")); return false;
	}
	if (Loaded == ERPGDepartureJournalLoad::Found)
	{
		PendingDepartureBackend = Restored.BackendUrl; PendingDepartureSteam = Restored.SteamId;
		PendingDepartureCharacter = Restored.CharacterId; PendingDepartureSession = Restored.SessionId;
		PendingDepartureRequest = Restored.RequestId; PendingDepartureRevision = Restored.LobbyRevision;
		bPendingDepartureCancel = Restored.bCancel;
	}
	bDepartureRecoveryBlocked = false;
	return true; // No POST, travel, membership, server address or credential is restored here.
}

bool URPGDungeonSessionSubsystem::PersistDepartureCommand()
{
	const auto Command = GetPendingDepartureCommand();
	FRPGDungeonDepartureJournal Journal(GetDepartureRecoveryDirectory(), Command.BackendUrl, Command.SteamId, Command.CharacterId);
	if (!IsDepartureScopeCurrent() || !Journal.Persist(Command))
	{
		bDepartureRecoveryBlocked = true; FailOperation(TEXT("departure_recovery_write_failed")); return false;
	}
	bDepartureRecoveryBlocked = false;
	return true;
}

bool URPGDungeonSessionSubsystem::ResolveDepartureCommand()
{
	if (!HasUnresolvedDeparture()) return true;
	// Older memory-only markers (also used by isolated native fixtures) have no disk record to remove.
	if (!PendingDepartureBackend.IsEmpty())
	{
		const auto Command = GetPendingDepartureCommand();
		FRPGDungeonDepartureJournal Journal(GetDepartureRecoveryDirectory(), Command.BackendUrl, Command.SteamId, Command.CharacterId);
		if (!IsDepartureScopeCurrent() || !Journal.Resolve(Command))
		{
			bDepartureRecoveryBlocked = true; FailOperation(TEXT("departure_recovery_resolve_failed")); return false;
		}
	}
	PendingDepartureBackend.Reset(); PendingDepartureSteam.Reset(); PendingDepartureCharacter.Reset();
	PendingDepartureSession.Reset(); PendingDepartureRequest.Reset(); PendingDepartureRevision = 0; bPendingDepartureCancel = false;
	bDepartureRecoveryBlocked = false;
	return true;
}
