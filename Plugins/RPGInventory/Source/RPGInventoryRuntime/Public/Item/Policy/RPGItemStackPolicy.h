#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGItemStackPolicy.generated.h"

class URPGItemInstance;
struct FRPGItemInstanceState;
struct FRPGItemStatValue;
struct FRPGItemRecordMetadata;

USTRUCT(BlueprintType)
struct RPGINVENTORYRUNTIME_API FRPGItemStackTransfer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 TransferredQuantity = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 SourceRemaining = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 DestinationQuantity = 0;

	bool HasTransfer() const { return TransferredQuantity > 0; }
};

/** Pure stack rules shared by inventory, equipment, pickup, and persistence. */
class RPGINVENTORYRUNTIME_API FRPGItemStackPolicy
{
public:
	static bool CanStack(
		const URPGItemInstance& First,
		const URPGItemInstance& Second);

	static bool AreInstanceStatesCompatible(
		const FRPGItemInstanceState& First,
		const FRPGItemInstanceState& Second);
	static bool AreInstanceValuesCompatible(
		const FGameplayTagContainer& FirstTags, const TArray<FRPGItemStatValue>& FirstStats,
		const FGameplayTagContainer& SecondTags, const TArray<FRPGItemStatValue>& SecondStats);
	static bool AreMetadataCompatible(
		const FRPGItemRecordMetadata& First, const FRPGItemRecordMetadata& Second);

	static FRPGItemStackTransfer CalculateTransfer(
		const URPGItemInstance& Source,
		const URPGItemInstance& Destination,
		int32 RequestedQuantity);
};
