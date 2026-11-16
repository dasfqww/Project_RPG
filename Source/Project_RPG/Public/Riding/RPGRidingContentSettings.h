#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RPGRidingContentSettings.generated.h"

class URPGMountDefinition;
class URPGRidingPolicy;

/** Shared authored defaults only. Ownership/admission remain server controlled. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="RPG Riding Content"))
class PROJECT_RPG_API URPGRidingContentSettings final : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** Empty keeps Riding unconfigured. Store production assets under /Game/Blueprints/Riding. */
    UPROPERTY(Config, EditAnywhere, Category="Content", meta=(ConfigRestartRequired=true))
    TSoftObjectPtr<URPGRidingPolicy> Policy;

    /** Whole-list fallback for Pawns without an explicit Definitions list. */
    UPROPERTY(Config, EditAnywhere, Category="Content", meta=(ConfigRestartRequired=true))
    TArray<TSoftObjectPtr<URPGMountDefinition>> MountDefinitions;

    /** Loads/validates policy metadata; never confirms an admission or installs ownership. */
    URPGRidingPolicy* LoadPolicy() const;
    /** Atomic output: failure clears Out, including missing/duplicate/invalid definitions. No cosmetic loads. */
    bool LoadDefinitions(TArray<TObjectPtr<URPGMountDefinition>>& Out) const;
};
