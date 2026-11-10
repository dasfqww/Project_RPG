#pragma once

#include "GameFramework/RootMotionSource.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingDashRootMotion.generated.h"

class ACharacter;

/** Only Riding owns this source type. Identity travels with snapshots, clones and server corrections. */
USTRUCT()
struct PROJECT_RPG_API FRootMotionSource_RPGRidingDash : public FRootMotionSource_ConstantForce
{
    GENERATED_BODY()
    FRootMotionSource_RPGRidingDash();
    UPROPERTY() FRPGRidingStateKey RidingKey;
    UPROPERTY() FGuid RidingExecution;
    bool HasValidIdentity() const;
    bool IsCurrent(const FRPGRidingRepState& State) const;
    virtual FRootMotionSource* Clone() const override;
    virtual UScriptStruct* GetScriptStruct() const override;
    virtual bool Matches(const FRootMotionSource* Other) const override;
    virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& Success) override;
};

template<> struct TStructOpsTypeTraits<FRootMotionSource_RPGRidingDash> : TStructOpsTypeTraitsBase2<FRootMotionSource_RPGRidingDash>
{ enum { WithNetSerializer = true, WithCopy = true }; };

namespace RPGRidingDashMotion
{
    /** No name/prefix matching: preserve every non-Riding source and all current executions. */
    PROJECT_RPG_API bool RejectStale(FRootMotionSourceGroup& Group, const FRPGRidingRepState& State);
    /** Native local reconciliation only; never installs state or trusts a received client stamp. */
    PROJECT_RPG_API void Reconcile(ACharacter& Character, const FRPGRidingRepState& State, bool IncludeSavedMoves = false);
}
