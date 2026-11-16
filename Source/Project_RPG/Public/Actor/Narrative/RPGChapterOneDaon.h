#pragma once
#include "CoreMinimal.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "Actor/Narrative/RPGNarrativeInvestigation.h"
#include "RPGChapterOneDaon.generated.h"

class UWidgetComponent;
class URPGChapterOneClueLabel;

/** Server-owned evidence target. No collision/UI: the visible NPC forwards the normal investigation. */
UCLASS()
class PROJECT_RPG_API ARPGChapterOneDiscovery : public ARPGNarrativeInvestigation
{
    GENERATED_BODY()
public:
    ARPGChapterOneDiscovery();
protected:
    virtual void BeginPlay() override;
};

/** Discovery and dialogue share one visible interaction point, but keep their authority boundaries. */
UCLASS()
class PROJECT_RPG_API ARPGChapterOneDaon : public ARPGNarrativeNPC
{
    GENERATED_BODY()
public:
    ARPGChapterOneDaon();
    virtual void Interact(APlayerController* Controller) override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Replicated) TObjectPtr<ARPGChapterOneDiscovery> Discovery;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Label;
    UPROPERTY(Transient) TObjectPtr<URPGChapterOneClueLabel> PersonalLabel;
    FGuid TrackingOffered;
    double NextRefresh = 0;
};
