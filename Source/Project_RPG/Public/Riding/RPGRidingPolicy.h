#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "RPGRidingPolicy.generated.h"

class FDataValidationContext;

/** Copied at policy admission. No UObject references or per-mount performance overrides. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGRidingPolicyValues
{
    GENERATED_BODY()
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) int32 ProtocolVersion = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) int32 CatalogVersion = 1;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FString GameplayHash;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float RideSpeed = 600;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float Acceleration = 2048;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float Braking = 2000;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float YawRate = 500;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float DashDistance = 900;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float DashDuration = 0.6f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float DashCooldown = 10;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float CombatLock = 10;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float EmoteDuration = 3;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) float EmoteCooldown = 2;

    bool Validate(TArray<FText>& Errors) const;
};

UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGRidingPolicy final : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Riding") FRPGRidingPolicyValues Values;
    /** A failure clears Out; an invalid/default policy must never silently activate Riding. */
    bool TryBuildRuntimeValues(FRPGRidingPolicyValues& Out, TArray<FText>& Errors) const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
