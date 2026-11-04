#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "RPGQuestManager.generated.h"

/** Game-instance service. Character-keyed, account-bound read models; backend owns quest rules. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGQuestManager final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") TArray<FRPGQuestProgress> GetQuests(FGuid CharacterId) const;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool IsQuestCompleted(FGuid CharacterId, const FString& QuestId) const;
    bool HasRevision(FGuid CharacterId, int64 Revision) const;
private:
    friend class URPGNarrativeCoordinator;
    bool Accepts(FGuid CharacterId, int64 Revision, const FString& Endpoint, const FString& SteamId) const;
    void Apply(const FRPGNarrativeSnapshot& Snapshot, const FString& Endpoint, const FString& SteamId);
    struct FCharacterView { int64 Revision = -1; TArray<FRPGQuestProgress> Progress; };
    TMap<FGuid, FCharacterView> Views;
    FString BoundEndpoint, BoundSteamId;
};
