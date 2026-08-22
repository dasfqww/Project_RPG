#pragma once

#include "CoreMinimal.h"
#include "RPGItemStackPolicy.generated.h"

class URPGItemInstance;
struct FRPGItemInstanceState;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemStackTransfer
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
class PROJECT_RPG_API FRPGItemStackPolicy
{
public:
	static bool CanStack(
		const URPGItemInstance& First,
		const URPGItemInstance& Second);

	static bool AreInstanceStatesCompatible(
		const FRPGItemInstanceState& First,
		const FRPGItemInstanceState& Second);

	static FRPGItemStackTransfer CalculateTransfer(
		const URPGItemInstance& Source,
		const URPGItemInstance& Destination,
		int32 RequestedQuantity);
};
