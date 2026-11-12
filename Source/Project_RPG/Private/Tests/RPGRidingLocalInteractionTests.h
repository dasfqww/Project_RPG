#pragma once
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGRidingLocalInteractionTests.generated.h"

UCLASS(Transient, NotBlueprintable)
class ARPGRidingLocalTarget : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGRidingLocalTarget();
    virtual bool CanApproveRidingLocalInteraction(APlayerController* Controller) const override;
    virtual FGuid GetRidingLocalInteractionIdentity() const override { return Identity; }
    FGuid Identity = FGuid::NewGuid();
    bool bAllowed = true;
};
