#include "Riding/RPGRidingContentSettings.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingPolicy.h"
#include "Riding/RPGRidingTypes.h"
#include "UObject/StrongObjectPtr.h"

URPGRidingPolicy* URPGRidingContentSettings::LoadPolicy() const
{
    if (!IsInGameThread() || Policy.IsNull()) return nullptr;
    auto* Loaded = Policy.LoadSynchronous();
    FRPGRidingPolicyValues Values;
    TArray<FText> Errors;
    return IsValid(Loaded) && Loaded->TryBuildRuntimeValues(Values, Errors) ? Loaded : nullptr;
}

bool URPGRidingContentSettings::LoadDefinitions(TArray<TObjectPtr<URPGMountDefinition>>& Out) const
{
    Out.Reset();
    if (!IsInGameThread() || MountDefinitions.IsEmpty() || MountDefinitions.Num() > RPGRiding::MaxMountDefinitions) return false;
    TArray<TStrongObjectPtr<URPGMountDefinition>> Held;
    TArray<const URPGMountDefinition*> Catalog;
    for (const auto& Reference : MountDefinitions)
    {
        if (Reference.IsNull()) return false;
        auto* Loaded = Reference.LoadSynchronous();
        if (!IsValid(Loaded)) return false;
        Held.Emplace(Loaded); // Protect earlier metadata if a later synchronous load collects garbage.
        Catalog.Add(Loaded);
    }
    TArray<FText> Errors;
    if (!URPGMountDefinition::ValidateCatalog(Catalog, Errors)) return false;
    for (const auto& Definition : Held) Out.Add(Definition.Get());
    return true;
}
