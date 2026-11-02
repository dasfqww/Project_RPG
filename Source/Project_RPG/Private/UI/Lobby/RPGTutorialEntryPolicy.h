#pragma once
#include "CoreMinimal.h"
#include "Manager/HttpWebManager.h"

enum class ERPGTutorialEntryAction : uint8 { Wait, Create, Depart, Connect, Reject };

// A decision over the existing authenticated session snapshot. This owns no
// membership, ticket, selected character, departure command or server state.
struct FRPGTutorialEntryPolicy
{
	static ERPGTutorialEntryAction Decide(const FRPGDungeonSession& Session,
		const FString& CharacterId, bool bBusy, bool bUnresolvedDeparture, bool bAdmissionBlocked,
		const FString& DungeonId = TEXT("Progression.Training"));
};
