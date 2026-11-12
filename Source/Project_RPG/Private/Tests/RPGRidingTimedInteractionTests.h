#pragma once
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGRidingTimedInteractionTests.generated.h"
UCLASS(Transient, NotBlueprintable)
class ARPGRidingTimedTarget : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGRidingTimedTarget();
    virtual bool CanApproveRidingTimedInteraction(APlayerController* Controller) const override;
    virtual FGuid GetRidingTimedInteractionIdentity() const override { return Identity; }
    FGuid Identity = FGuid::NewGuid();
    bool bAllowed = true;
};
