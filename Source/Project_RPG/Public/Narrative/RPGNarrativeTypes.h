#pragma once

#include "CoreMinimal.h"
#include "RPGNarrativeTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGQuestStatus : uint8 { Active, ReadyToReport, Completed };

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGQuestObjectiveView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FText Text;
    UPROPERTY(BlueprintReadOnly) bool bCompleted = false;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGQuestProgress
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString QuestId;
    UPROPERTY(BlueprintReadOnly) int32 DefinitionVersion = 0;
    UPROPERTY(BlueprintReadOnly) ERPGQuestStatus Status = ERPGQuestStatus::Active;
    UPROPERTY(BlueprintReadOnly) TArray<FString> CompletedObjectives;
    UPROPERTY(BlueprintReadOnly) FText Title;
    UPROPERTY(BlueprintReadOnly) TArray<FRPGQuestObjectiveView> Objectives;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDialogueChoice
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FText Text;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDialogueNode
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString Id;
    UPROPERTY(BlueprintReadOnly) FText Speaker;
    UPROPERTY(BlueprintReadOnly) FText Text;
    UPROPERTY(BlueprintReadOnly) TArray<FRPGDialogueChoice> Choices;
};

struct PROJECT_RPG_API FRPGNarrativeSnapshot
{
    FGuid CharacterId;
    int32 CatalogVersion = 0;
    int64 Revision = 0;
    TArray<FRPGQuestProgress> Quests;
    TArray<FString> CompletedChapters;
    TArray<FString> StoryUnlocks;
    TArray<FRPGDialogueNode> AvailableDialogues;
};

struct PROJECT_RPG_API FRPGNarrativeCommand
{
    FGuid RequestId;
    int64 ExpectedRevision = 0;
    int32 CatalogVersion = 0;
    FString DialogueId;
    FString ChoiceId;
};

struct PROJECT_RPG_API FRPGNarrativeHttpResult
{
    bool bCurrentPlayerContext = false;
    bool bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
};

class PROJECT_RPG_API FRPGNarrativeCodec
{
public:
    static bool Decode(const FString& Body, FGuid ExpectedCharacter, FRPGNarrativeSnapshot& Out);
    static bool Encode(const FRPGNarrativeCommand& Command, FString& OutBody);
};
