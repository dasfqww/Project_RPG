#include "Item/Definition/RPGItemDefinition.h"

#include "Item/RPGItemRuntimeTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemDefinition)

const FPrimaryAssetType URPGItemDefinition::PrimaryAssetType(
	TEXT("RPGItemDefinition"));

void URPGItemDefinitionFragment::BuildInstanceState(
	FRPGItemInstanceStateBuilder& Builder,
	FRandomStream& RandomStream) const
{
}

#if WITH_EDITOR
EDataValidationResult URPGItemDefinitionFragment::ValidateDefinition(
	const URPGItemDefinition& Definition,
	FDataValidationContext& Context) const
{
	return EDataValidationResult::Valid;
}
#endif

FPrimaryAssetId URPGItemDefinition::GetPrimaryAssetId() const
{
	const FPrimaryAssetId StableId = MakePrimaryAssetIdForTag(ItemTag);
	return StableId.IsValid()
		? StableId
		: FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId URPGItemDefinition::MakePrimaryAssetIdForTag(
	const FGameplayTag& InItemTag)
{
	return InItemTag.IsValid()
		? FPrimaryAssetId(PrimaryAssetType, InItemTag.GetTagName())
		: FPrimaryAssetId();
}

#if WITH_EDITOR
EDataValidationResult URPGItemDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const auto Fail = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!ItemTag.IsValid())
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "InvalidItemTag",
			"Item Tag must be valid."));
	}
	if (DefinitionVersion < 1)
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "InvalidDefinitionVersion",
			"Definition Version must be at least one."));
	}
	if (ItemCategory == EItemCategory::None)
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "InvalidCategory",
			"Item Category cannot be None."));
	}
	if (DisplayName.IsEmpty())
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "EmptyDisplayName",
			"Display Name cannot be empty."));
	}
	if (MaxStackSize < 1)
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "InvalidMaxStack",
			"Max Stack Size must be at least one."));
	}
	if (GridSize.X < 1 || GridSize.Y < 1)
	{
		Fail(NSLOCTEXT("RPGItemDefinition", "InvalidGridSize",
			"Both Grid Size dimensions must be at least one."));
	}

	TSet<const UClass*> UniqueFragmentClasses;
	for (const URPGItemDefinitionFragment* Fragment : Fragments)
	{
		if (!IsValid(Fragment))
		{
			Fail(NSLOCTEXT("RPGItemDefinition", "NullFragment",
				"Fragments cannot contain null entries."));
			continue;
		}

		const UClass* FragmentClass = Fragment->GetClass();
		if (!Fragment->AllowsMultipleFragments() &&
			UniqueFragmentClasses.Contains(FragmentClass))
		{
			Fail(FText::Format(
				NSLOCTEXT("RPGItemDefinition", "DuplicateFragment",
					"Fragment class {0} is duplicated."),
				FText::FromString(FragmentClass->GetName())));
		}
		UniqueFragmentClasses.Add(FragmentClass);

		if (Fragment->ValidateDefinition(*this, Context) ==
			EDataValidationResult::Invalid)
		{
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

void URPGItemDefinition::BuildInstanceState(
	const int32 RequestedQuantity,
	const int32 GenerationSeed,
	FRPGItemInstanceState& OutState) const
{
	OutState.Initialize(
		GenerationSeed,
		FMath::Clamp(RequestedQuantity, 1, GetMaxStackSize()));

	FRandomStream RandomStream(GenerationSeed);
	FRPGItemInstanceStateBuilder Builder(OutState);
	for (const URPGItemDefinitionFragment* Fragment : Fragments)
	{
		if (IsValid(Fragment))
		{
			Fragment->BuildInstanceState(Builder, RandomStream);
		}
	}
}

const URPGItemDefinitionFragment* URPGItemDefinition::FindFragmentByClass(
	const TSubclassOf<URPGItemDefinitionFragment> FragmentClass) const
{
	if (!FragmentClass)
	{
		return nullptr;
	}

	for (const URPGItemDefinitionFragment* Fragment : Fragments)
	{
		if (IsValid(Fragment) && Fragment->IsA(FragmentClass))
		{
			return Fragment;
		}
	}
	return nullptr;
}
