#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Combat/RPGHitReactionTypes.h"
#include "RPGHitReactionProfile.generated.h"

/** Reusable animation configuration for compatible skeleton/AnimBP setups. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGHitReactionProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	TMap<ERPGHitReactionDirection, FRPGHitReactionMontage> DirectionMontages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation")
	FRPGHitReactionMontage FallbackMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Presentation",
		meta = (ClampMin = "0.05", ClampMax = "1.0", Units = "s"))
	float MaximumPresentationAge = 0.5f;
};
