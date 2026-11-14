#pragma once
#include "Riding/RPGRidingCollection.h"

struct PROJECT_RPG_API FRPGRidingRegistrationOption
{
    FString DefinitionType, DefinitionName, MountId;
    int32 DefinitionVersion = 0, MountDefinitionVersion = 0, RequiredQuantity = 0, MaximumStackSize = 0;
    bool bAlreadyOwned = false;
};

/** Definition mapping and observed ownership only; inventory/lease/CAS validation belongs to the registration POST. */
struct PROJECT_RPG_API FRPGRidingRegistrationOptions
{
    FGuid CharacterId, RosterId;
    int64 RosterRevision = 0;
    bool bCatalogReady = false, bRegistrationEnabled = false;
    int32 CatalogVersion = 0;
    FString GameplayHash;
    TArray<FRPGRidingRegistrationOption> Options;
    const FRPGRidingRegistrationOption* Find(const FString& Type, const FString& Name, int32 Version) const;
    bool MatchesCollection(const FRPGRidingCollection& Collection) const;
};
namespace RPGRidingRegistrationOptions
{
    constexpr int32 MaximumBytes = 256 * 1024, MaximumOptions = 512;
    PROJECT_RPG_API bool Decode(const FString& Body, FGuid ExpectedCharacter, FRPGRidingRegistrationOptions& Out);
}
