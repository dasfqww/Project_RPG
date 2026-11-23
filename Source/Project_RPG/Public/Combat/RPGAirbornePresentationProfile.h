#pragma once
#include "Engine/DataAsset.h"
#include "Combat/RPGAirborneTypes.h"
#include "RPGAirbornePresentationProfile.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAirborneDirectionalMontages
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Airborne;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Downed;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage GettingUp;
};

/** Cosmetic clips only. No gameplay notify or animation root motion is allowed. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGAirbornePresentationProfile : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Airborne;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage Downed;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FRPGHitReactionMontage GettingUp;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TMap<ERPGHitReactionDirection, FRPGAirborneDirectionalMontages> DirectionMontages;

	const FRPGHitReactionMontage& ResolveMontage(ERPGAirbornePhase Phase, ERPGHitReactionDirection Direction) const
	{
		const auto* Clips = DirectionMontages.Find(Direction);
		switch (Phase)
		{
		case ERPGAirbornePhase::Airborne:
			return Clips && Clips->Airborne.Montage ? Clips->Airborne : Airborne;
		case ERPGAirbornePhase::Downed:
			return Clips && Clips->Downed.Montage ? Clips->Downed : Downed;
		case ERPGAirbornePhase::GettingUp:
			return Clips && Clips->GettingUp.Montage ? Clips->GettingUp : GettingUp;
		default:
			checkNoEntry();
			return Airborne;
		}
	}
};
