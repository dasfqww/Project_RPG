#pragma once
#include "Riding/RPGRidingTypes.h"

/** Native server values, copied from the admitted policy. Not RPC/BP authoring data. */
struct FRPGRidingDashProfile
{
    float Distance = 0.f;
    float Duration = 0.f;
};

/** One admitted execution. Consumers must recheck currency after any gameplay callback. */
struct FRPGRidingDashTicket
{
    FRPGRidingStateKey Key;
    FGuid ExecutionId;
    FRPGRidingDashProfile Profile;
};
