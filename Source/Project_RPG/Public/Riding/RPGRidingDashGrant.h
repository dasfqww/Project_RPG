#pragma once
#include "Riding/RPGRidingTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "GameplayPrediction.h"
#include "RPGRidingDashGrant.generated.h"

/** Owner-only server copy. It describes a capability; possessing the value never grants authority. */
USTRUCT()
struct PROJECT_RPG_API FRPGRidingDashGrant
{
    GENERATED_BODY()
    UPROPERTY() FGuid GrantId;
    UPROPERTY() FGuid RiderEpoch;
    UPROPERTY() uint32 MountRevision = 0;
    UPROPERTY() FGameplayAbilitySpecHandle AbilityHandle;
    UPROPERTY() int32 CatalogVersion = 0;
    UPROPERTY() float Distance = 0;
    UPROPERTY() float Duration = 0;
    UPROPERTY() float Cooldown = 0;
    bool IsEmpty() const;
    bool IsValid() const;
    bool Matches(const FRPGRidingRepState& State) const;
    bool SameGrant(const FRPGRidingDashGrant& Other) const;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& Success);
};
template<> struct TStructOpsTypeTraits<FRPGRidingDashGrant> : TStructOpsTypeTraitsBase2<FRPGRidingDashGrant>
{ enum { WithNetSerializer = true, WithCopy = true }; };

/** Captured only from an actual GAS invocation. No RPC/BP execution-id or permission setter. */
struct PROJECT_RPG_API FRPGRidingDashActivation
{
    FRPGRidingDashGrant Grant;
    FPredictionKey PredictionKey;
    bool IsEmpty() const { return Grant.IsEmpty() && PredictionKey.Current == 0; }
    bool IsValid() const { return Grant.IsValid() && PredictionKey.IsValidKey(); }
    FGuid ExecutionFor(const FRPGRidingStateKey& Expected) const;
};
