#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/RPGNarrativeStorySkip.h"
#include "Narrative/RPGNarrativeHistory.h"
#include "RPGNarrativeStorySkipPanel.generated.h"

class UComboBoxString;
class UTextBlock;
class UButton;

/** Explicit chapter selection -> server preview -> personal confirmation. No implicit write. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeStorySkipPanel final : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(TSharedPtr<FRPGNarrativeSkipClient> InClient, TSharedPtr<FRPGNarrativeHistoryClient> InHistory, FGuid InCharacter);
    FText GetDisplayedPreview() const;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    friend struct FRPGNarrativeSkipUiTestAccess;
    UFUNCTION() void PreviewChapter();
    UFUNCTION() void ConfirmChapter();
    UFUNCTION() void CancelReview();
    UFUNCTION() void RetryPending();
    void Render();
    void Unbind();
    FString SelectedChapter() const;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Chapters;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
    UPROPERTY(Transient) TObjectPtr<UButton> PreviewButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    TSharedPtr<FRPGNarrativeSkipClient> Client;
    TSharedPtr<FRPGNarrativeHistoryClient> History;
    FDelegateHandle ClientChanged, HistoryChanged;
    TArray<FString> CandidateIds;
    FGuid Character;
    int64 LastRevision = -1;
    int32 LastSelection = INDEX_NONE;
    bool bLastSelected = false, bLastCanStart = false, bLastReview = false;
};
