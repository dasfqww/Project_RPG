#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "Containers/Ticker.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "RPGNarrativeCoordinator.generated.h"

class URPGQuestManager;
class URPGScenarioManager;
class URPGDialogueManager;
class IRPGNarrativeTransport;
class FRPGNarrativeHistoryClient;
class URPGDungeonSessionSubsystem;
struct FRPGDungeonSession;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGNarrativeChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRPGNarrativeRequestFinished, bool, bSuccess, const FString&, ErrorCode, bool, bOutcomeUnknown);

/** Local-player facade. Backend managers own authoritative rule evaluation and commits. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeCoordinator final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool SelectCharacter(FGuid CharacterId);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool Refresh();
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool BeginDialogue(const FString& DialogueId);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") void CloseDialogue();
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool Choose(const FString& ChoiceId);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool RetryPendingChoice();
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool HasCurrentSnapshot() const;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") FGuid GetSelectedCharacterId() const { return SelectedCharacter; }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool IsBusy() const { return bBusy; }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool HasPendingChoice() const { return PendingChoice.IsSet() || PendingDungeon.IsSet(); }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool HasPendingDungeonObjective() const { return PendingDungeon.IsSet(); }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") FString GetPendingDialogueId() const { return PendingChoice ? PendingChoice->DialogueId : FString(); }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") URPGQuestManager* GetQuestManager() const { return Quests; }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") URPGScenarioManager* GetScenarioManager() const { return Scenarios; }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") URPGDialogueManager* GetDialogueManager() const { return Dialogues; }
    UPROPERTY(BlueprintAssignable, Category="RPG|Narrative") FRPGNarrativeChanged OnChanged;
    UPROPERTY(BlueprintAssignable, Category="RPG|Narrative") FRPGNarrativeRequestFinished OnRequestFinished;
    TSharedPtr<FRPGNarrativeHistoryClient> GetAccountHistoryClient();
private:
    friend struct FRPGNarrativeTestAccess;
    bool BindManagers();
    bool EnsureTransport();
    bool ContextMatches() const;
    bool Dispatch(bool bWrite, bool bDungeon = false);
    void Receive(uint64 Generation, bool bWrite, FRPGNarrativeHttpResult Result);
    void Apply(FRPGNarrativeSnapshot Snapshot);
    void ClearView();
    UFUNCTION() void HandleDungeonSessionChanged(const FRPGDungeonSession& Session);
    UFUNCTION() void HandleDungeonFlowChanged(ERPGDungeonClientFlowState State, const FString& Error);
    void ScheduleDungeonSync(bool bRefresh, bool bResetBudget = false);
    bool TickDungeonSync(float DeltaSeconds);
    UPROPERTY(Transient) TObjectPtr<URPGQuestManager> Quests;
    UPROPERTY(Transient) TObjectPtr<URPGScenarioManager> Scenarios;
    UPROPERTY(Transient) TObjectPtr<URPGDialogueManager> Dialogues;
    UPROPERTY(Transient) TObjectPtr<URPGDungeonSessionSubsystem> DungeonFlow;
    TSharedPtr<IRPGNarrativeTransport> Transport;
    TSharedPtr<FRPGNarrativeHistoryClient> HistoryClient;
    TOptional<FRPGNarrativeCommand> PendingChoice;
    TOptional<FRPGNarrativeDungeonCommand> PendingDungeon;
    TArray<FRPGNarrativeDungeonCandidate> DungeonCandidates;
    FTSTicker::FDelegateHandle DungeonSyncTick;
    int32 DungeonSyncBudget = 0;
    FGuid LastObservedDungeonResult;
    bool bDungeonRefreshNeeded = false;
    FString PendingBody, Endpoint, SteamId;
    FGuid SelectedCharacter;
    int64 Revision = 0;
    int32 CatalogVersion = 0;
    uint64 RequestGeneration = 0;
    bool bBusy = false, bHasView = false, bShuttingDown = false;
};
