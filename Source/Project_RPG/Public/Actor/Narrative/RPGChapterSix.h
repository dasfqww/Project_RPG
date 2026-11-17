#pragma once
#include "CoreMinimal.h"
#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "RPGChapterSix.generated.h"
class UWidgetComponent;
class URPGChapterOneClueLabel;

/** Same personal observation, with distinct fresh-run and historical-story entry points. */
UCLASS()
class PROJECT_RPG_API ARPGChapterSixPatient:public ARPGNarrativeInvestigation
{
    GENERATED_BODY()
public:
    ARPGChapterSixPatient();
    bool IsRecallPoint() const {return bRecallPoint;}
    virtual bool CanObserve(APlayerController* Controller) const override;
    virtual void Tick(float Delta) override;
protected:
    UPROPERTY(EditAnywhere,Category="M06") bool bRecallPoint=false;
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
    UPROPERTY(Transient) TObjectPtr<URPGChapterOneClueLabel> PersonalLabel;
};
UCLASS()
class PROJECT_RPG_API ARPGChapterSixDaon:public ARPGNarrativeNPC
{
    GENERATED_BODY()
public:
    ARPGChapterSixDaon();
    virtual void Tick(float Delta) override;
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
    UPROPERTY(Transient) TObjectPtr<URPGChapterOneClueLabel> PersonalLabel;
    FGuid TrackingOffered;
    double NextRefresh=0;
};
