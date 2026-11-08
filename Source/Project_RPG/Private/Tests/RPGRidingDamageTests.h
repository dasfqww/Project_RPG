#pragma once
#include "GameFramework/PlayerController.h"
#include "GenericTeamAgentInterface.h"
#include "RPGRidingDamageTests.generated.h"

UCLASS(Transient, NotBlueprintable)
class ARPGRidingDamageTestController : public APlayerController, public IGenericTeamAgentInterface
{
    GENERATED_BODY()
public:
    virtual FGenericTeamId GetGenericTeamId() const override { return Team; }
    FGenericTeamId Team = FGenericTeamId(1);
};
