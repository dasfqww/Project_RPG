#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "RPGNarrativeQuestTrackerWidget.generated.h"

class UComboBoxString;
class UButton;
class UTextBlock;

/** Journal controls or a hit-test-invisible HUD view of the same local selection. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeQuestTrackerWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(TSharedPtr<FRPGNarrativeQuestTracking> InModel, FGuid InCharacter, bool bInControls);
    FText GetDisplayedTracking();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
    virtual void NativeDestruct() override;
private:
    friend struct FRPGNarrativeTrackingTestAccess;
    UFUNCTION() void TrackSelected();
    UFUNCTION() void ClearTracking();
    void Render();
    TSharedPtr<FRPGNarrativeQuestTracking> Model;
    FGuid Character;
    bool bControls = true;
    float Elapsed = 0.f;
    FString OptionsKey;
    TArray<FString> CandidateIds;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> Quests;
    UPROPERTY(Transient) TObjectPtr<UButton> TrackButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ClearButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Summary;
};
