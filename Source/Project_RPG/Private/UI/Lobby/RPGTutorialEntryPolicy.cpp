#include "UI/Lobby/RPGTutorialEntryPolicy.h"

ERPGTutorialEntryAction FRPGTutorialEntryPolicy::Decide(const FRPGDungeonSession& Session,
	const FString& CharacterId, bool bBusy, bool bUnresolvedDeparture, bool bAdmissionBlocked, const FString& DungeonId)
{
	if (DungeonId != TEXT("Progression.Training") && DungeonId != TEXT("Progression.Intro1")) return ERPGTutorialEntryAction::Reject;
	if (CharacterId.IsEmpty() || bAdmissionBlocked || bUnresolvedDeparture) return ERPGTutorialEntryAction::Reject;
	if (bBusy) return ERPGTutorialEntryAction::Wait;
	if (Session.DungeonSessionId.IsEmpty())
		return Session.State.IsEmpty() ? ERPGTutorialEntryAction::Create : ERPGTutorialEntryAction::Reject;
	if (!Session.DungeonId.Equals(DungeonId, ESearchCase::CaseSensitive)
		|| !Session.Difficulty.Equals(TEXT("Normal"), ESearchCase::CaseSensitive)
		|| Session.Members.Num() != 1 || !Session.Members[0].CharacterId.Equals(CharacterId, ESearchCase::CaseSensitive))
		return ERPGTutorialEntryAction::Reject;
	if (Session.State.Equals(TEXT("Waiting"), ESearchCase::CaseSensitive))
	{
		if (!Session.LeaderCharacterId.Equals(CharacterId, ESearchCase::CaseSensitive)) return ERPGTutorialEntryAction::Reject;
		return Session.DepartureRequestedAt.IsEmpty() ? ERPGTutorialEntryAction::Depart : ERPGTutorialEntryAction::Wait;
	}
	if (Session.State.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)) return ERPGTutorialEntryAction::Wait;
	if (Session.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive))
		return !Session.ServerId.TrimStartAndEnd().IsEmpty() && !Session.ServerAddress.TrimStartAndEnd().IsEmpty()
			? ERPGTutorialEntryAction::Connect : ERPGTutorialEntryAction::Wait;
	return ERPGTutorialEntryAction::Reject;
}
