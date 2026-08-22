#include "Economy/RPGDungeonRewardDefinition.h"

#include "Item/Definition/RPGItemDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace RPGDungeonRewardDefinition
{
	constexpr int32 MaximumCurrencyChangeCount = 16;
	constexpr int32 MaximumItemRewardCount = 16;
	constexpr int32 MaximumInstanceTagCount = 64;
	constexpr int32 MaximumStatValueCount = 128;

	bool IsSimpleIdentifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 64)
		{
			return false;
		}

		for (const TCHAR Character : Value)
		{
			const bool bAsciiAlphaNumeric =
				(Character >= TEXT('A') && Character <= TEXT('Z'))
				|| (Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'));
			if (!bAsciiAlphaNumeric
				&& Character != TEXT('.')
				&& Character != TEXT('_')
				&& Character != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}
}

bool URPGDungeonRewardDefinition::BuildSettlement(
	FString& OutRewardVersion,
	TArray<FRPGCurrencyChange>& OutCurrencyChanges,
	TArray<FRPGDungeonItemReward>& OutItemRewards,
	FString& OutError) const
{
	OutRewardVersion.Reset();
	OutCurrencyChanges.Reset();
	OutItemRewards.Reset();
	OutError.Reset();

	const FString Version = RewardVersion.ToString().TrimStartAndEnd();
	if (!RPGDungeonRewardDefinition::IsSimpleIdentifier(Version))
	{
		OutError = TEXT("RewardVersion must be a simple ASCII identifier of at most 64 characters.");
		return false;
	}
	if (CurrencyChanges.IsEmpty() && ItemRewards.IsEmpty())
	{
		OutError = TEXT("A configured reward definition cannot be empty; use the explicit no-reward clear path instead.");
		return false;
	}
	if (CurrencyChanges.Num() >
		RPGDungeonRewardDefinition::MaximumCurrencyChangeCount)
	{
		OutError = TEXT("A reward definition cannot contain more than 16 currency changes.");
		return false;
	}
	if (ItemRewards.Num() > RPGDungeonRewardDefinition::MaximumItemRewardCount)
	{
		OutError = TEXT("A reward definition cannot contain more than 16 item rewards.");
		return false;
	}

	TSet<FString> CurrencyCodes;
	for (int32 Index = 0; Index < CurrencyChanges.Num(); ++Index)
	{
		const FRPGCurrencyChange& Change = CurrencyChanges[Index];
		const FString CurrencyCode = Change.CurrencyCode.ToString();
		if (!RPGDungeonRewardDefinition::IsSimpleIdentifier(CurrencyCode)
			|| Change.Delta <= 0
			|| CurrencyCodes.Contains(CurrencyCode))
		{
			OutError = FString::Printf(
				TEXT("Currency reward %d must have a unique identifier and a positive delta."),
				Index);
			return false;
		}
		CurrencyCodes.Add(CurrencyCode);
	}

	TArray<FRPGDungeonItemReward> BuiltItemRewards;
	BuiltItemRewards.Reserve(ItemRewards.Num());
	for (int32 Index = 0; Index < ItemRewards.Num(); ++Index)
	{
		const FRPGDungeonItemRewardEntry& Entry = ItemRewards[Index];
		const URPGItemDefinition* Definition = Entry.ItemDefinition;
		if (!IsValid(Definition) || !Definition->ItemTag.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Item reward %d requires an item definition with a valid ItemTag."),
				Index);
			return false;
		}

		const FPrimaryAssetId DefinitionId = Definition->GetPrimaryAssetId();
		if (!DefinitionId.IsValid() || Definition->GetDefinitionVersion() < 1)
		{
			OutError = FString::Printf(
				TEXT("Item reward %d has an invalid persistent definition identity."),
				Index);
			return false;
		}
		if (Entry.Quantity < 1
			|| Entry.Quantity > Definition->GetMaxStackSize())
		{
			OutError = FString::Printf(
				TEXT("Item reward %d quantity must be within the definition's stack limit (1-%d)."),
				Index,
				Definition->GetMaxStackSize());
			return false;
		}
		if (!Entry.Durability.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Item reward %d has invalid durability."),
				Index);
			return false;
		}
		if (Entry.InstanceTags.Num() >
			RPGDungeonRewardDefinition::MaximumInstanceTagCount
			|| Entry.StatValues.Num() >
				RPGDungeonRewardDefinition::MaximumStatValueCount)
		{
			OutError = FString::Printf(
				TEXT("Item reward %d contains too much instance metadata."),
				Index);
			return false;
		}

		TSet<FGameplayTag> StatTags;
		for (const FRPGDungeonItemRewardStat& Stat : Entry.StatValues)
		{
			if (!Stat.StatTag.IsValid()
				|| !FMath::IsFinite(Stat.Value)
				|| StatTags.Contains(Stat.StatTag))
			{
				OutError = FString::Printf(
					TEXT("Item reward %d has an invalid or duplicate stat value."),
					Index);
				return false;
			}
			StatTags.Add(Stat.StatTag);
		}

		FRPGDungeonItemReward& Reward = BuiltItemRewards.AddDefaulted_GetRef();
		Reward.DefinitionType =
			FName(*DefinitionId.PrimaryAssetType.ToString());
		Reward.DefinitionName = DefinitionId.PrimaryAssetName;
		Reward.DefinitionVersion = Definition->GetDefinitionVersion();
		Reward.Quantity = Entry.Quantity;
		Reward.BindState = Entry.BindState;
		Reward.Durability = Entry.Durability;
		Reward.InstanceTags = Entry.InstanceTags;
		Reward.StatValues = Entry.StatValues;
	}

	OutRewardVersion = Version;
	OutCurrencyChanges = CurrencyChanges;
	OutItemRewards = MoveTemp(BuiltItemRewards);
	return true;
}

#if WITH_EDITOR
EDataValidationResult URPGDungeonRewardDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FString BuiltVersion;
	TArray<FRPGCurrencyChange> BuiltCurrencyChanges;
	TArray<FRPGDungeonItemReward> BuiltItemRewards;
	FString Error;
	if (!BuildSettlement(
		BuiltVersion,
		BuiltCurrencyChanges,
		BuiltItemRewards,
		Error))
	{
		Context.AddError(FText::FromString(Error));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif
