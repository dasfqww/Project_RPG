#pragma once
#include "GameFramework/Pawn.h"
#include "RPGRidingActionBarTests.generated.h"

class URPGRidingComponent;
class UQuickSlotComponent;

/** No entitlement grants or server policy overrides. Replication injection is dev-test-only. */
UCLASS(Transient, NotBlueprintable)
class ARPGRidingActionBarTestPawn : public APawn
{
    GENERATED_BODY()
public:
    ARPGRidingActionBarTestPawn();
    UPROPERTY() TObjectPtr<URPGRidingComponent> Riding;
    UPROPERTY() TObjectPtr<UQuickSlotComponent> Slots;
};
