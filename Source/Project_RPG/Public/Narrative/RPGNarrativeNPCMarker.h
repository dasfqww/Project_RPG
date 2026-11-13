#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"

enum class ERPGNarrativeNPCMarker : uint8 { None, Dialogue, Progress, Accept, Report };
struct PROJECT_RPG_API FRPGNarrativeNPCMarker
{
    static ERPGNarrativeNPCMarker Resolve(const TArray<FString>& EntryIds, const TArray<FRPGDialogueNode>& Available);
    static FText Label(ERPGNarrativeNPCMarker Marker);
};
