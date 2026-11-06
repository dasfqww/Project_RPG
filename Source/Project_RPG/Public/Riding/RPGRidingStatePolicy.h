#pragma once

#include "Riding/RPGRidingTypes.h"

enum class ERPGRidingTransition : uint8 { None, Mount, Replace, Dismount };

/** Server-built values only, never an RPC payload. Candidate resolution/randomness belongs to the session. */
struct PROJECT_RPG_API FRPGRidingEvaluationContext
{
    FRPGRidingRepState Current;
    bool bHasAuthority = false;
    bool bAuthorityRevoked = false;
    bool bReady = false;
    bool bAlive = false;
    bool bOnGround = false;
    bool bActionBlocked = false;
    bool bSlowActive = false;
    bool bInCombat = false;
    bool bCandidateOwned = false;
    bool bDefinitionCompatible = false;
    bool bRegionAllowed = false;
    FString CandidateMountId;
    int32 CandidateDefinitionVersion = 0;
};

struct PROJECT_RPG_API FRPGRidingDecision
{
    ERPGRidingResultCode Result = ERPGRidingResultCode::NotReady;
    ERPGRidingTransition Transition = ERPGRidingTransition::None;
    FString ResolvedMountId;
    int32 DefinitionVersion = 0;
};

namespace RPGRidingStatePolicy
{
    /** No allocation by request size beyond bounded IDs; no clock, RNG, UObject, mutation or effects. */
    PROJECT_RPG_API FRPGRidingDecision Evaluate(const FRPGRidingEvaluationContext& Context, const FRPGRidingSetCommand& Command);
}
