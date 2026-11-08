#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "RPGRidingWorldPolicy.generated.h"

class ACharacter;

struct FRPGRidingPathPermission
{
    bool bAllowed = false;
    double SafeFraction = 0;
};

/** Default Deny; reads loaded server-authored regions on every query, including policy/base movement changes. */
UCLASS()
class PROJECT_RPG_API URPGRidingWorldPolicy final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    FRPGRidingPathPermission EvaluatePath(const ACharacter& Rider, const FVector& Start, const FVector& End,
        const FGameplayTagContainer& MountTags) const;
};
