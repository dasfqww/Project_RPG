#include "Item/Policy/RPGItemStackPolicy.h"

#include "Item/RPGItemInstance.h"
#include "Item/RPGItemRuntimeTypes.h"
#include "Item/Persistence/RPGItemRecord.h"

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
	// Equipment progression is per-instance and must never be merged into a stack.
	return !First.GetEquipmentProgression().IsSet() && !Second.GetEquipmentProgression().IsSet() &&
		AreInstanceValuesCompatible(First.GetInstanceTags(), First.GetStatValues(),
		Second.GetInstanceTags(), Second.GetStatValues());
}

bool FRPGItemStackPolicy::AreMetadataCompatible(
	const FRPGItemRecordMetadata& First, const FRPGItemRecordMetadata& Second)
{
	// Do not wash binding, durability or expiration by moving units into another stack.
	// CreationSource remains an audit label on each original record, not a stack property.
	return First.BindState == Second.BindState &&
		First.Durability.Current == Second.Durability.Current &&
		First.Durability.Maximum == Second.Durability.Maximum &&
		First.ExpiresAtUtc == Second.ExpiresAtUtc;
}

bool FRPGItemStackPolicy::AreInstanceValuesCompatible(
	const FGameplayTagContainer& FirstTags, const TArray<FRPGItemStatValue>& FirstStats,
	const FGameplayTagContainer& SecondTags, const TArray<FRPGItemStatValue>& SecondStats)
{
	if (!(FirstTags == SecondTags))
	{
		return false;
	}
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
