#pragma once
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingDashIntent.generated.h"

/** Bounded input only. The server supplies distance, time, cooldown and starting location. */
USTRUCT()
struct PROJECT_RPG_API FRPGRidingDashIntent : public FGameplayAbilityTargetData
{
    GENERATED_BODY()
    // Iris target data uses reflected members instead of this type's native NetSerialize.
    UPROPERTY() FGuid GrantId;
    UPROPERTY() FRPGRidingStateKey Expected;
    UPROPERTY() FVector Direction = FVector::ZeroVector;
    virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
    bool IsValid() const;
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& Success);
    static const FRPGRidingDashIntent* Read(const FGameplayAbilityTargetDataHandle& Data);
};
template<> struct TStructOpsTypeTraits<FRPGRidingDashIntent> : TStructOpsTypeTraitsBase2<FRPGRidingDashIntent>
{ enum { WithNetSerializer = true, WithCopy = true }; };
