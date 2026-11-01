#pragma once

#include "CoreMinimal.h"

/** Presentation categories, never a source of permission or server state. */
enum class ERPGOnlineLobbyNextStepState : uint8
{
	Unavailable, ConfirmingAbandonment, Busy, Login, Reauthenticate, Character,
	InvalidCharacterName, AbandonmentPending, DepartureStorageBlocked, DepartureUnconfirmed,
	MembershipCheck, RequestFailed, SessionCreation, SteamParty, Recruiting, DepartureQueued,
	ServerLoading, ServerStarted, SettlementPending, UnknownSession, CharacterSelected
};

struct FRPGOnlineLobbyNextStep
{
	FText Text;
	FName TargetControl = NAME_None;
};

struct PROJECT_RPG_API FRPGOnlineLobbyNextStepPresentation
{
	/** Targets are an explicit safe allowlist intersected with enabled, visible controls. */
	static FRPGOnlineLobbyNextStep Build(ERPGOnlineLobbyNextStepState State, const TSet<FName>& AvailableControls);
};
