#include "Item/RPGItemRuntimeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemRuntimeTypes)

bool FRPGItemInstanceState::IsValid() const
{
	return InstanceId.IsValid() && Quantity > 0;
}

bool FRPGItemInstanceState::TryRestore(
	const FGuid& InInstanceId,
	const int32 InGenerationSeed,
	const int32 InQuantity,
	const FGameplayTagContainer& InInstanceTags,
	const TArray<FRPGItemStatValue>& InStatValues,
	FRPGItemInstanceState& OutState)
{
	if (!InInstanceId.IsValid() || InQuantity < 0)
	{
		return false;
	}

	TSet<FGameplayTag> UniqueStatTags;
	for (const FRPGItemStatValue& StatValue : InStatValues)
	{
		if (!StatValue.StatTag.IsValid() ||
			!FMath::IsFinite(StatValue.Value) ||
			UniqueStatTags.Contains(StatValue.StatTag))
		{
			return false;
		}
		UniqueStatTags.Add(StatValue.StatTag);
	}

	FRPGItemInstanceState RestoredState;
	RestoredState.InstanceId = InInstanceId;
	RestoredState.GenerationSeed = InGenerationSeed;
	RestoredState.Quantity = InQuantity;
	RestoredState.InstanceTags = InInstanceTags;
	RestoredState.StatValues = InStatValues;
	OutState = MoveTemp(RestoredState);
	return true;
}

float FRPGItemInstanceState::GetStatValue(
	const FGameplayTag& StatTag,
	const float DefaultValue) const
{
	const FRPGItemStatValue* StatValue = StatValues.FindByPredicate(
		[StatTag](const FRPGItemStatValue& Candidate)
		{
			return Candidate.StatTag.MatchesTagExact(StatTag);
		});
	if (StatValue)
	{
		return StatValue->Value;
	}
	return DefaultValue;
}

bool FRPGItemInstanceState::HasInstanceTag(const FGameplayTag& Tag) const
{
	return Tag.IsValid() && InstanceTags.HasTagExact(Tag);
}

void FRPGItemInstanceState::Initialize(
	const int32 InGenerationSeed,
	const int32 InQuantity)
{
	InstanceId = FGuid::NewGuid();
	GenerationSeed = InGenerationSeed;
	Quantity = InQuantity;
	InstanceTags.Reset();
	StatValues.Reset();
}

void FRPGItemInstanceState::SetQuantity(const int32 InQuantity)
{
	Quantity = InQuantity;
}

bool FRPGItemInstanceStateBuilder::AddInstanceTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return false;
	}

	State.InstanceTags.AddTag(Tag);
	return true;
}

bool FRPGItemInstanceStateBuilder::AddStatValue(
	const FGameplayTag& StatTag,
	const float Value)
{
	if (!StatTag.IsValid() || !FMath::IsFinite(Value))
	{
		return false;
	}

	FRPGItemStatValue* ExistingValue = State.StatValues.FindByPredicate(
		[StatTag](const FRPGItemStatValue& Candidate)
		{
			return Candidate.StatTag.MatchesTagExact(StatTag);
		});
	if (ExistingValue)
	{
		ExistingValue->Value += Value;
	}
	else
	{
		FRPGItemStatValue& NewValue = State.StatValues.AddDefaulted_GetRef();
		NewValue.StatTag = StatTag;
		NewValue.Value = Value;
	}
	return true;
}
