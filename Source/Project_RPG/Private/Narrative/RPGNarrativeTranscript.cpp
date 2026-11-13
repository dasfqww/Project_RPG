#include "Narrative/RPGNarrativeTranscript.h"

bool FRPGNarrativeTranscript::Context(FGuid Character, FRPGNarrativeTranscriptContext& Out) const
{
    FRPGNarrativeTranscriptContext Current;
    if (!Provider || !Provider(Current) || !Character.IsValid() || Current.Character != Character
        || Current.Account.IsEmpty() || Current.Account.Len() > 128 || Current.Endpoint.IsEmpty() || Current.Endpoint.Len() > 2048) return false;
    Out = MoveTemp(Current); return true;
}
bool FRPGNarrativeTranscript::Matches(const FEntry& Entry, const FRPGNarrativeTranscriptContext& Current)
{
    return Entry.Character == Current.Character && Entry.Account.Equals(Current.Account, ESearchCase::CaseSensitive)
        && Entry.Endpoint.Equals(Current.Endpoint, ESearchCase::CaseSensitive);
}
bool FRPGNarrativeTranscript::Capture(FGuid Character, const FString& DisplayedNode)
{
    FRPGNarrativeTranscriptContext Current;
    if (DisplayedNode.IsEmpty() || DisplayedNode.Len() > 128 || !Context(Character, Current)
        || !Current.bHasActive || !Current.Active.Id.Equals(DisplayedNode, ESearchCase::CaseSensitive)) return false;
    const auto Speaker = Current.Active.Speaker.ToString(), Text = Current.Active.Text.ToString();
    if (Speaker.IsEmpty() || Speaker.Len() > 256 || Text.IsEmpty() || Text.Len() > 4096) return false;
    if (!Entries.IsEmpty())
    {
        const auto& Last = Entries.Last();
        if (Matches(Last, Current) && Last.Node.Equals(DisplayedNode, ESearchCase::CaseSensitive)
            && Last.Line.Speaker.ToString().Equals(Speaker, ESearchCase::CaseSensitive) && Last.Line.Text.ToString().Equals(Text, ESearchCase::CaseSensitive)) return false;
    }
    const int32 Size = Speaker.Len() + Text.Len();
    Entries.Add({Current.Character, MoveTemp(Current.Account), MoveTemp(Current.Endpoint), DisplayedNode, {Current.Active.Speaker, Current.Active.Text}, Size});
    Characters += Size;
    while (Entries.Num() > MaximumLines || Characters > MaximumCharacters) { Characters -= Entries[0].Characters; Entries.RemoveAt(0); }
    return true;
}
bool FRPGNarrativeTranscript::Read(FGuid Character, TArray<FRPGNarrativeTranscriptLine>& Out) const
{
    Out.Reset(); FRPGNarrativeTranscriptContext Current;
    if (!Context(Character, Current)) return false;
    for (const auto& Entry : Entries) if (Matches(Entry, Current)) Out.Add(Entry.Line);
    return true;
}
bool FRPGNarrativeTranscript::Clear(FGuid Character)
{
    FRPGNarrativeTranscriptContext Current; if (!Context(Character, Current)) return false;
    Entries.RemoveAll([&](const auto& Entry) { return Matches(Entry, Current); });
    Characters = 0; for (const auto& Entry : Entries) Characters += Entry.Characters;
    return true;
}
void FRPGNarrativeTranscript::Reset() { Entries.Reset(); Characters = 0; }
