#include "Item/Policy/RPGItemStackPolicy.h"

#include "Item/RPGItemInstance.h"
#include "Item/RPGItemRuntimeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemStackPolicy)

bool FRPGItemStackPolicy::CanStack(
	const URPGItemInstance& First,
	const URPGItemInstance& Second)
{
	return First.GetDefinition() != nullptr &&
		First.GetDefinition() == Second.GetDefinition() &&
		First.IsStackable() &&
		Second.IsStackable() &&
		AreInstanceStatesCompatible(First.GetState(), Second.GetState());
}

bool FRPGItemStackPolicy::AreInstanceStatesCompatible(
	const FRPGItemInstanceState& First,
	const FRPGItemInstanceState& Second)
{
	if (!(First.GetInstanceTags() == Second.GetInstanceTags()))
	{
		return false;
	}

	const TArray<FRPGItemStatValue>& FirstStats = First.GetStatValues();
	const TArray<FRPGItemStatValue>& SecondStats = Second.GetStatValues();
	if (FirstStats.Num() != SecondStats.Num())
	{
		return false;
	}

	for (const FRPGItemStatValue& Stat : FirstStats)
	{
		const FRPGItemStatValue* OtherValue = SecondStats.FindByPredicate(
			[&Stat](const FRPGItemStatValue& Candidate)
			{
				return Candidate.StatTag.MatchesTagExact(Stat.StatTag);
			});
		if (!OtherValue || OtherValue->Value != Stat.Value)
		{
			return false;
		}
	}
	return true;
}

FRPGItemStackTransfer FRPGItemStackPolicy::CalculateTransfer(
	const URPGItemInstance& Source,
	const URPGItemInstance& Destination,
	const int32 RequestedQuantity)
{
	FRPGItemStackTransfer Result;
	Result.SourceRemaining = Source.GetQuantity();
	Result.DestinationQuantity = Destination.GetQuantity();

	if (RequestedQuantity <= 0 || !CanStack(Source, Destination))
	{
		return Result;
	}

	const int32 DestinationCapacity = FMath::Max(
		0,
		Destination.GetMaxStackSize() - Destination.GetQuantity());
	Result.TransferredQuantity = FMath::Min3(
		RequestedQuantity,
		Source.GetQuantity(),
		DestinationCapacity);
	Result.SourceRemaining -= Result.TransferredQuantity;
	Result.DestinationQuantity += Result.TransferredQuantity;
	return Result;
}
