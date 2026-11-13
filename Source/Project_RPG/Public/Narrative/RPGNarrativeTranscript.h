#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"

struct PROJECT_RPG_API FRPGNarrativeTranscriptContext
{
    FGuid Character;
    FString Account, Endpoint;
    bool bHasActive = false;
    FRPGDialogueNode Active;
};
struct PROJECT_RPG_API FRPGNarrativeTranscriptLine
{
    FText Speaker, Text;
};

/** Bounded text already shown this session, never a choice/completion receipt or replay command. */
class PROJECT_RPG_API FRPGNarrativeTranscript
{
public:
    using FProvider = TFunction<bool(FRPGNarrativeTranscriptContext&)>;
    explicit FRPGNarrativeTranscript(FProvider InProvider) : Provider(MoveTemp(InProvider)) {}
    static constexpr int32 MaximumLines = 64;
    static constexpr int32 MaximumCharacters = 32768;
    bool Capture(FGuid Character, const FString& DisplayedNode);
    bool Read(FGuid Character, TArray<FRPGNarrativeTranscriptLine>& Out) const;
    bool Clear(FGuid Character);
    void Reset();
private:
    struct FEntry { FGuid Character; FString Account, Endpoint, Node; FRPGNarrativeTranscriptLine Line; int32 Characters = 0; };
    bool Context(FGuid Character, FRPGNarrativeTranscriptContext& Out) const;
    static bool Matches(const FEntry& Entry, const FRPGNarrativeTranscriptContext& Current);
    FProvider Provider;
    TArray<FEntry> Entries;
    int32 Characters = 0;
};
