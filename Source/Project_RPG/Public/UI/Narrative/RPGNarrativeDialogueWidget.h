#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/RPGWidgetBase.h"
#include "RPGNarrativeDialogueWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class URPGNarrativeCoordinator;
class URPGNarrativeHistoryPanel;
class URPGNarrativeStorySkipPanel;
class ARPGNarrativeNPC;
class APawn;
class FRPGWindowInputState;

DECLARE_DELEGATE_TwoParams(FRPGNarrativeAction, const FString&, const FString&);

/** One authored action. IDs stay in the control model rather than the visible label. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeActionWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(const FText& Label, const FString& DialogueId, const FString& ChoiceId, FRPGNarrativeAction Action);
    const FString& GetDialogueId() const { return NodeId; }
    const FString& GetChoiceId() const { return OptionId; }
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UFUNCTION() void Click();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LabelWidget;
    FText Caption;
    FString NodeId, OptionId;
    FRPGNarrativeAction OnAction;
};

/** Owner-local story menu. Only server-provided dialogue/actions are selectable. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeDialogueWidget final : public URPGWidgetBase
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool OpenForCharacter(FGuid CharacterId);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool OpenForNPC(FGuid CharacterId, ARPGNarrativeNPC* NPC);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") void CloseScreen();
    void FocusNPCInput();
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") FGuid GetCharacterId() const { return Character; }
    UFUNCTION(BlueprintPure, Category="RPG|Narrative") FText GetDisplayedStatus() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    friend struct FRPGNarrativeTestAccess;
    bool OpenContext(FGuid CharacterId);
    bool EnsureNPCContext();
    void ReleaseNPCInput();
    UFUNCTION() void RefreshStory();
    UFUNCTION() void RetryChoice();
    UFUNCTION() void HandleChanged();
    UFUNCTION() void HandleFinished(bool bSuccess, const FString& Error, bool bOutcomeUnknown);
    void Act(const FString& DialogueId, const FString& ChoiceId);
    void Render();
    void Unbind();
    bool MatchesSelection() const;
    void AddAction(const FText& Label, const FString& DialogueId, const FString& ChoiceId, bool bEnabled);
    UPROPERTY(Transient) TObjectPtr<URPGNarrativeCoordinator> Coordinator;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Actions;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Speaker;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Body;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Progress;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Journal;
    UPROPERTY(Transient) TObjectPtr<URPGNarrativeHistoryPanel> AccountHistory;
    UPROPERTY(Transient) TObjectPtr<URPGNarrativeStorySkipPanel> StorySkip;
    UPROPERTY(Transient) TObjectPtr<UButton> RefreshButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    FGuid Character;
    FText FailureText;
    FString OwnedDialogue;
    bool bLastReadable = false, bLastBusy = false, bLastPending = false;
    bool bNPCContext = false;
    TWeakObjectPtr<ARPGNarrativeNPC> SourceNPC;
    TWeakObjectPtr<APawn> SourcePawn;
    FString NPCAccount, NPCEndpoint;
    TSharedPtr<FRPGWindowInputState> NPCInput;
};
