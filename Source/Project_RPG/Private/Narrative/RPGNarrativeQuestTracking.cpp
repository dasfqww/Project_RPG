#include "Narrative/RPGNarrativeQuestTracking.h"

bool FRPGNarrativeQuestTracking::Context(FRPGNarrativeTrackingContext& Out) const
{
    FRPGNarrativeTrackingContext Current;
    if (!Provider || !Provider(Current) || !Current.Character.IsValid() || Current.Endpoint.IsEmpty() || Current.Account.IsEmpty()) return false;
    Out = MoveTemp(Current); return true;
}
bool FRPGNarrativeQuestTracking::Matches(const FRPGNarrativeTrackingContext& Current) const
{
    return Character == Current.Character && Endpoint.Equals(Current.Endpoint, ESearchCase::CaseSensitive)
        && Account.Equals(Current.Account, ESearchCase::CaseSensitive);
}
bool FRPGNarrativeQuestTracking::Track(const FGuid OwnerCharacter, const FString& Id)
{
    FRPGNarrativeTrackingContext Current;
    if (!Context(Current) || Current.Character != OwnerCharacter || !Current.bReadable) return false;
    const auto* Quest = Current.Quests.FindByPredicate([&](const auto& Q) { return Q.QuestId.Equals(Id, ESearchCase::CaseSensitive); });
    if (!Quest || Quest->Status == ERPGQuestStatus::Completed) return false;
    Character = Current.Character; Endpoint = MoveTemp(Current.Endpoint); Account = MoveTemp(Current.Account); QuestId = Id;
    return true;
}
bool FRPGNarrativeQuestTracking::Clear(const FGuid OwnerCharacter)
{
    FRPGNarrativeTrackingContext Current;
    if (!Context(Current) || Current.Character != OwnerCharacter || !Matches(Current)) return false;
    Reset(); return true;
}
bool FRPGNarrativeQuestTracking::Read(FRPGQuestProgress& Out)
{
    FRPGNarrativeTrackingContext Current;
    if (QuestId.IsEmpty() || !Context(Current) || !Matches(Current) || !Current.bReadable) return false;
    const auto* Quest = Current.Quests.FindByPredicate([&](const auto& Q) { return Q.QuestId.Equals(QuestId, ESearchCase::CaseSensitive); });
    // Only an authoritative readable snapshot can retire a pin, never a query/auth failure.
    if (!Quest || Quest->Status == ERPGQuestStatus::Completed) { Reset(); return false; }
    Out = *Quest; return true;
}
void FRPGNarrativeQuestTracking::Reset() { Character.Invalidate(); Endpoint.Reset(); Account.Reset(); QuestId.Reset(); }
