#include "Item/Legacy/RPGLegacyItemDefinitionAdapter.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "RPGItemTags.h"

namespace RPGLegacyItemDefinitionAdapter
{
void SetError(FString* OutError, const TCHAR* Message)
{
	if (OutError)
	{
		*OutError = Message;
	}
}

FText MakeFallbackDisplayName(const FGameplayTag& ItemTag)
{
	FString Name = ItemTag.ToString();
	int32 SeparatorIndex = INDEX_NONE;
	if (Name.FindLastChar(TEXT('.'), SeparatorIndex))
	{
		Name.RightChopInline(SeparatorIndex + 1, EAllowShrinking::No);
	}
	return FText::FromString(Name);
}
}

bool FRPGLegacyItemDefinitionAdapter::TryBuildDefinitionView(
	const FItemManifest& Manifest,
	FRPGItemDefinitionView& OutView,
	FString* OutError)
{
	OutView = FRPGItemDefinitionView();
	const FGameplayTag ItemTag = Manifest.GetItemTag();
	if (!ItemTag.IsValid())
	{
		RPGLegacyItemDefinitionAdapter::SetError(
			OutError,
			TEXT("A legacy item manifest must have a valid item tag."));
		return false;
	}
	if (Manifest.GetItemCategory() == EItemCategory::None)
	{
		RPGLegacyItemDefinitionAdapter::SetError(
			OutError,
			TEXT("A legacy item manifest must have an inventory category."));
		return false;
	}

	OutView.Snapshot.DefinitionId =
		URPGItemDefinition::MakePrimaryAssetIdForTag(ItemTag);
	OutView.Snapshot.DefinitionVersion = 1;
	if (const FStackableFragment* Stackable =
		Manifest.GetFragmentOfType<FStackableFragment>())
	{
		OutView.Snapshot.MaxStackSize =
			FMath::Max(1, Stackable->GetMaxQuantity());
	}

	OutView.ItemTag = ItemTag;
	OutView.ItemCategory = Manifest.GetItemCategory();
	OutView.DisplayName =
		RPGLegacyItemDefinitionAdapter::MakeFallbackDisplayName(ItemTag);
	if (const FTextFragment* NameFragment =
		Manifest.GetFragmentOfTypeByTag<FTextFragment>(
			RPGGameplayTags::Fragment_ItemNameFragment))
	{
		if (!NameFragment->GetText().IsEmpty())
		{
			OutView.DisplayName = NameFragment->GetText();
		}
	}
	if (const FImageFragment* IconFragment =
		Manifest.GetFragmentOfTypeByTag<FImageFragment>(
			RPGGameplayTags::Fragment_IconFragment))
	{
		OutView.Icon = IconFragment->GetIcon();
	}
	OutView.bLegacySource = true;

	if (!OutView.IsValid())
	{
		RPGLegacyItemDefinitionAdapter::SetError(
			OutError,
			TEXT("The legacy item manifest could not produce a valid definition view."));
		OutView = FRPGItemDefinitionView();
		return false;
	}
	return true;
}

