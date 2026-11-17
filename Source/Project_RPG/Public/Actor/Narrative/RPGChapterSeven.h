#pragma once
#include "CoreMinimal.h"
#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "RPGChapterSeven.generated.h"
class UWidgetComponent;
class URPGChapterOneClueLabel;

/** Personal extraction operations. Backend prerequisites bind them to the saved boss receipt. */
UCLASS()
class PROJECT_RPG_API ARPGChapterSevenOperation : public ARPGNarrativeInvestigation
{
    GENERATED_BODY()
public:
    ARPGChapterSevenOperation();
    virtual void Tick(float Delta) override;
    virtual bool CanObserve(APlayerController* Controller) const override;
    UPROPERTY(EditAnywhere, Category="M07") int32 Step=0;
    FString Objective() const;
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
    UPROPERTY(Transient) TObjectPtr<URPGChapterOneClueLabel> PersonalLabel;
    double NextRefresh=0;
};

UCLASS()
class PROJECT_RPG_API ARPGChapterSevenDaon : public ARPGNarrativeNPC
{
    GENERATED_BODY()
public:
    ARPGChapterSevenDaon();
};
