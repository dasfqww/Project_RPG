#pragma once

#include "CoreMinimal.h"
#include <initializer_list>

/** Explanations only; none of these values grants permission to run an action. */
enum class ERPGOnlineLobbyBlockReason : uint8
{
	None, Unavailable, ServiceDraining, ConfirmationOpen, Busy, LoginRequired, ReauthenticationRequired,
	AlreadyAuthenticated, LogoutCredentialRequired, CharacterRequired, ActiveSession,
	MemberSessionRequired, SteamPartyActive, SteamPartyRequired, WaitingRequired,
	LeaderRequired, LobbyProtocolRequired, DepartureQueued, DepartureNotQueued,
	DepartureUnconfirmed, NoPendingDeparture, DepartureStorageBlocked, AbandonmentUnconfirmed,
	AdmissionBlocked, ServerNotReady, DungeonSelectionRequired, PartySelectionRequired,
	PartyNotJoinable, InviteRequired, DevelopmentOnly, StateCheckRequired, SteamDisabled, UnsavedConfiguration, CreationClassRequired
};

struct FRPGOnlineLobbyActionRequirement
{
	bool bSatisfied;
	ERPGOnlineLobbyBlockReason MissingReason;
};

struct PROJECT_RPG_API FRPGOnlineLobbyActionAvailability
{
	/** Existing enabled result is authoritative. Missing explanations never alter it. */
	static ERPGOnlineLobbyBlockReason Explain(bool bEnabled, ERPGOnlineLobbyBlockReason CommonReason,
		std::initializer_list<FRPGOnlineLobbyActionRequirement> Requirements = {});
	static FText Text(ERPGOnlineLobbyBlockReason Reason);
};
