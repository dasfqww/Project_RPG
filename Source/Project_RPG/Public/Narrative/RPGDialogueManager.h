#pragma once
#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "RPGDialogueManager.generated.h"

UCLASS(BlueprintType)
class PROJECT_RPG_API URPGDialogueManager final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") TArray<FRPGDialogueNode> GetAvailableDialogues() const;
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") bool GetActiveDialogue(FRPGDialogueNode& OutDialogue) const;
private:
    friend class URPGNarrativeCoordinator;
    bool Readable() const;
    void Reset();
    UPROPERTY(Transient) TArray<FRPGDialogueNode> Dialogues;
    FString ActiveId;
    FString BoundEndpoint, BoundSteamId;
    FGuid CharacterId;
    int64 Revision = -1;
};
