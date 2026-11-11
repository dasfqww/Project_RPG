#pragma once
#include "Riding/RPGRidingTypes.h"

/** Server policy and one admitted cosmetic execution. Never supplied by a client. */
struct FRPGRidingEmoteProfile
{
    float Duration = 0;
    float Cooldown = 0;
};
struct FRPGRidingEmoteTicket
{
    FRPGRidingStateKey Key;
    FGuid ExecutionId;
};
