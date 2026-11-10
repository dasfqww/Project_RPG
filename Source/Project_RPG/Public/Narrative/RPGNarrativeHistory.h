#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"

class IRPGNarrativeTransport;
struct PROJECT_RPG_API FRPGNarrativeChapterHistory
{
    FString Id;
    FText Title;
    bool bSelected = false, bOther = false;
};
struct PROJECT_RPG_API FRPGNarrativeAccountHistory
{
    FGuid Character;
    int32 CatalogVersion = 0;
    TArray<FRPGNarrativeChapterHistory> Chapters;
    static bool Decode(const FString& Json, FGuid ExpectedCharacter, FRPGNarrativeAccountHistory& Out);
};
enum class ERPGNarrativeHistoryState : uint8 { Idle, Loading, Ready, Failed };
DECLARE_MULTICAST_DELEGATE(FRPGNarrativeHistoryChanged);

/** Independent read channel. It cannot send choices or overwrite the character journal. */
class PROJECT_RPG_API FRPGNarrativeHistoryClient : public TSharedFromThis<FRPGNarrativeHistoryClient>
{
public:
    FRPGNarrativeHistoryClient(TSharedRef<IRPGNarrativeTransport> InTransport, TFunction<bool(FGuid)> InSelection);
    ~FRPGNarrativeHistoryClient();
    bool Refresh(FGuid Character);
    void Reset();
    bool IsSelected(FGuid Character) const;
    ERPGNarrativeHistoryState GetState(FGuid Character) const;
    const FRPGNarrativeAccountHistory* GetView(FGuid Character) const;
    FRPGNarrativeHistoryChanged OnChanged;
private:
    void Receive(uint64 ExpectedGeneration, FRPGNarrativeHttpResult Result);
    bool ContextMatches() const;
    TSharedRef<IRPGNarrativeTransport> Transport;
    TFunction<bool(FGuid)> Selection;
    FRPGNarrativeAccountHistory View;
    FGuid Character;
    FString Endpoint, Steam;
    uint64 Generation = 0;
    ERPGNarrativeHistoryState State = ERPGNarrativeHistoryState::Idle;
};
