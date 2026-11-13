#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"

struct PROJECT_RPG_API FRPGNarrativeTrackingContext
{
    FGuid Character;
    FString Endpoint, Account;
    bool bReadable = false;
    TArray<FRPGQuestProgress> Quests;
};

/** Local display selection only. Provider exposes an already validated personal snapshot. */
class PROJECT_RPG_API FRPGNarrativeQuestTracking
{
public:
    using FProvider = TFunction<bool(FRPGNarrativeTrackingContext&)>;
    explicit FRPGNarrativeQuestTracking(FProvider InProvider) : Provider(MoveTemp(InProvider)) {}
    bool Context(FRPGNarrativeTrackingContext& Out) const;
    bool Track(FGuid Character, const FString& QuestId);
    bool Clear(FGuid Character);
    bool Read(FRPGQuestProgress& Out);
    FString PinnedId() const { return QuestId; }
    void Reset();
private:
    bool Matches(const FRPGNarrativeTrackingContext& Current) const;
    FProvider Provider;
    FGuid Character;
    FString Endpoint, Account, QuestId;
};
