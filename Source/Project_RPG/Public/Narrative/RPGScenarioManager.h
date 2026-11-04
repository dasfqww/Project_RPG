#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGScenarioManager.generated.h"

/** Story eligibility only; combat admission remains with the existing server flow. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGScenarioManager final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool IsChapterCompleted(FGuid CharacterId, const FString& ChapterId) const;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool IsStoryUnlocked(FGuid CharacterId, const FString& UnlockId) const;
    bool HasRevision(FGuid CharacterId, int64 Revision) const;
private:
    friend class URPGNarrativeCoordinator;
    bool Accepts(FGuid CharacterId, int64 Revision, const FString& Endpoint, const FString& SteamId) const;
    void Apply(const struct FRPGNarrativeSnapshot& Snapshot, const FString& Endpoint, const FString& SteamId);
    struct FCharacterView { int64 Revision = -1; TArray<FString> CompletedChapters, StoryUnlocks; };
    TMap<FGuid, FCharacterView> Views;
    FString BoundEndpoint, BoundSteamId;
};
