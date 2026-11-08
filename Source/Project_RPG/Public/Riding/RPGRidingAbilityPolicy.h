#pragma once
#include "UObject/Interface.h"
#include "RPGRidingAbilityPolicy.generated.h"

/** Native ability classification, never a BP/client permission setter. */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class URPGRidingAbilityPolicy : public UInterface { GENERATED_BODY() };

class PROJECT_RPG_API IRPGRidingAbilityPolicy
{
    GENERATED_BODY()
public:
    virtual bool IsAutomaticRidingExempt() const = 0;
    virtual bool IsRidingActionAbility() const { return false; }
};
