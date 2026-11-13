#pragma once
#include "Riding/RPGRidingTypes.h"

struct PROJECT_RPG_API FRPGRidingOwnedMount
{
    FString MountId;
    int32 DefinitionVersion = 0;
    FDateTime UnlockedAtUtc;
    FGuid SourceRequestId;
};

/** Owner-facing persistent observation. This value never grants a server Riding session authority. */
struct PROJECT_RPG_API FRPGRidingCollection
{
    FGuid CharacterId, RosterId;
    int64 RosterRevision = 0, PreferencesRevision = 0;
    bool bCatalogReady = false, bRegistrationEnabled = false, bPreferencesEnabled = false;
    int32 CatalogVersion = 0;
    FString GameplayHash, SelectedMountId;
    TArray<FRPGRidingOwnedMount> Mounts;
    TArray<FString> Favorites;
    FRPGRidingSelector Shortcut;
};
namespace RPGRidingCollection
{
    constexpr int32 MaximumBytes = 256 * 1024;
    PROJECT_RPG_API bool Decode(const FString& Body, FGuid ExpectedCharacter, FRPGRidingCollection& Out);
}
