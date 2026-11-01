#pragma once
#include "Engine/DataAsset.h"
#include "Combat/RPGHitReactionProfile.h"
#include "RPGAirbornePresentationProfile.generated.h"

/** Cosmetic clips only. No gameplay notify or animation root motion is allowed. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGAirbornePresentationProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Airborne;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Downed;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage GettingUp;
};
