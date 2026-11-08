#include "Riding/RPGRidingRegionVolume.h"
#include "Components/BoxComponent.h"

ARPGRidingRegionVolume::ARPGRidingRegionVolume()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
#if WITH_EDITOR
    SetIsSpatiallyLoaded(false); // Policy must survive streaming of the playable geometry it protects.
#endif
    Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PolicyBounds"));
    SetRootComponent(Bounds);
    Bounds->SetBoxExtent(FVector(500));
    Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Bounds->SetGenerateOverlapEvents(false);
    Bounds->SetCanEverAffectNavigation(false);
    Bounds->SetHiddenInGame(true);
}
