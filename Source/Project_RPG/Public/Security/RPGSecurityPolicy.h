#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGSecurityPolicy.generated.h"

/**
 * Designer-authored server security policy.
 *
 * Assign an asset of this type to the player's Security Validation Component.
 * Different player Blueprints or GameModes may select separate PvE, PvP, and
 * development policies without changing the authoritative implementation.
 */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGSecurityPolicy : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Identity")
	FGameplayTag PolicyTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Policy",
		meta = (ShowOnlyInnerProperties))
	FRPGSecurityPolicyConfig Config;

	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	FRPGSecurityPolicyConfig GetPolicyConfig() const { return Config; }
};

