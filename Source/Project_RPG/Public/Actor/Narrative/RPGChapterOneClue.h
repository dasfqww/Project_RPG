#pragma once
#include "CoreMinimal.h"
#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "Blueprint/UserWidget.h"
#include "RPGChapterOneClue.generated.h"

class UTextBlock;
class UWidgetComponent;

UCLASS()
class PROJECT_RPG_API URPGChapterOneClueLabel : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetCaption(const FText& Value);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Caption;
    FText PendingCaption;
};

/** Authored M03 clue. Presentation reads personal committed progress; base actor owns interaction authority. */
UCLASS()
class PROJECT_RPG_API ARPGChapterOneClue : public ARPGNarrativeInvestigation
{
    GENERATED_BODY()
public:
    ARPGChapterOneClue();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
    UPROPERTY(EditAnywhere, Category="Chapter One", meta=(ClampMin="1", ClampMax="3")) int32 ClueNumber = 1;
private:
    void ConfigureClue();
    FGuid TrackingOfferedFor;
    FGuid ApproachTrackingOfferedFor;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
    UPROPERTY(Transient) TObjectPtr<URPGChapterOneClueLabel> PersonalLabel;
};
