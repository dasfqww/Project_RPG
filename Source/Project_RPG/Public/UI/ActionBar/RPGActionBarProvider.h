#pragma once

#include "CoreMinimal.h"
#include "UI/ActionBar/RPGActionBarProfile.h"

class APawn;

/**
 * Feature adapter for an already approved gameplay state. Implementations hold weak
 * references to their Pawn/component. The feature owns state replication, server
 * authorization, key mapping and cooldowns; this interface does not grant abilities.
 */
class PROJECT_RPG_API IRPGActionBarProvider
{
public:
    virtual ~IRPGActionBarProvider() = default;
    virtual APawn* GetActionBarPawn() const = 0;
    virtual const URPGActionBarProfile* GetActionBarProfile() const = 0;
    virtual FRPGActionBarActionView ReadAction(FName ActionId) const = 0;
    virtual void PressAction(FName ActionId) = 0;
    virtual void ReleaseAction(FName ActionId) = 0;
};
