#pragma once
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "RPGRidingRegionVolume.generated.h"

class UBoxComponent;

/** Server-authored policy geometry. No collision, overlap events, replication or client permission RPC. */
UCLASS()
class PROJECT_RPG_API ARPGRidingRegionVolume : public AActor
{
    GENERATED_BODY()
public:
    ARPGRidingRegionVolume();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Riding") TObjectPtr<UBoxComponent> Bounds;
    /** Empty means unrestricted. Overlapping nonempty Allow sets must share an exact mount tag. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Riding") FGameplayTagContainer AllowMountTags;
    virtual bool IsDenyRegion() const { return false; }
};

UCLASS()
class PROJECT_RPG_API ARPGNoRidingVolume final : public ARPGRidingRegionVolume
{
    GENERATED_BODY()
public:
    virtual bool IsDenyRegion() const override { return true; }
};
