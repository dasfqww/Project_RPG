#include "Narrative/RPGNarrativeNPCMarker.h"

ERPGNarrativeNPCMarker FRPGNarrativeNPCMarker::Resolve(const TArray<FString>& Entries, const TArray<FRPGDialogueNode>& Available)
{
    auto Result = ERPGNarrativeNPCMarker::None;
    for (const auto& Node : Available)
    {
        if (!Node.bEntryPoint || Node.Choices.IsEmpty() || !Entries.ContainsByPredicate([&](const auto& Id) { return Id.Equals(Node.Id, ESearchCase::CaseSensitive); })) continue;
        const auto Kind = Node.QuestAction == ERPGNarrativeQuestAction::Report ? ERPGNarrativeNPCMarker::Report
            : Node.QuestAction == ERPGNarrativeQuestAction::Accept ? ERPGNarrativeNPCMarker::Accept
            : Node.QuestAction == ERPGNarrativeQuestAction::Progress ? ERPGNarrativeNPCMarker::Progress : ERPGNarrativeNPCMarker::Dialogue;
        if (static_cast<uint8>(Kind) > static_cast<uint8>(Result)) Result = Kind;
    }
    return Result;
}
FText FRPGNarrativeNPCMarker::Label(ERPGNarrativeNPCMarker Marker)
{
    switch (Marker)
    {
    case ERPGNarrativeNPCMarker::Report: return NSLOCTEXT("RPGNarrative", "NPCReportMarker", "[보고 가능]");
    case ERPGNarrativeNPCMarker::Accept: return NSLOCTEXT("RPGNarrative", "NPCAcceptMarker", "[새 의뢰]");
    case ERPGNarrativeNPCMarker::Progress: return NSLOCTEXT("RPGNarrative", "NPCProgressMarker", "[의뢰 진행]");
    case ERPGNarrativeNPCMarker::Dialogue: return NSLOCTEXT("RPGNarrative", "NPCDialogueMarker", "[대화 가능]");
    default: return FText::GetEmpty();
    }
}
