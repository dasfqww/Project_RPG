#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Item/Legacy/RPGLegacyItemDefinitionAdapter.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "RPGItemTags.h"
#include "UObject/UnrealType.h"

namespace RPGLegacyItemDefinitionAdapterTests
{
bool SetManifestIdentity(
	FItemManifest& Manifest,
	const FGameplayTag& ItemTag,
	const EItemCategory ItemCategory)
{
	FStructProperty* ItemTagProperty = FindFProperty<FStructProperty>(
		FItemManifest::StaticStruct(),
		TEXT("ItemTag"));
	FEnumProperty* CategoryProperty = FindFProperty<FEnumProperty>(
		FItemManifest::StaticStruct(),
		TEXT("ItemCategory"));
	if (!ItemTagProperty || !CategoryProperty)
	{
		return false;
	}

	*ItemTagProperty->ContainerPtrToValuePtr<FGameplayTag>(&Manifest) =
		ItemTag;
	void* CategoryValue =
		CategoryProperty->ContainerPtrToValuePtr<void>(&Manifest);
	CategoryProperty->GetUnderlyingProperty()->SetIntPropertyValue(
		CategoryValue,
		static_cast<uint64>(ItemCategory));
	return true;
}

bool AddStackFragment(FItemManifest& Manifest, const int32 MaxStackSize)
{
	FStackableFragment Stackable;
	Stackable.SetFragmentTag(
		RPGGameplayTags::Fragment_StackableFragment);
	FNumericProperty* MaxQuantityProperty =
		FindFProperty<FNumericProperty>(
			FStackableFragment::StaticStruct(),
			TEXT("MaxQuantity"));
	if (!MaxQuantityProperty)
	{
		return false;
	}
	MaxQuantityProperty->SetIntPropertyValue(
		MaxQuantityProperty->ContainerPtrToValuePtr<void>(&Stackable),
		static_cast<int64>(MaxStackSize));

	TInstancedStruct<FItemFragment> Fragment;
	Fragment.InitializeAs<FStackableFragment>(Stackable);
	Manifest.GetFragmentsMutable().Add(MoveTemp(Fragment));
	return true;
}

void AddNameFragment(FItemManifest& Manifest, const TCHAR* DisplayName)
{
	FTextFragment Name;
	Name.SetFragmentTag(RPGGameplayTags::Fragment_ItemNameFragment);
	Name.SetText(FText::FromString(DisplayName));
	TInstancedStruct<FItemFragment> Fragment;
	Fragment.InitializeAs<FTextFragment>(Name);
	Manifest.GetFragmentsMutable().Add(MoveTemp(Fragment));
}

bool MakeLegacyManifest(
	FItemManifest& OutManifest,
	const int32 MaxStackSize = 37)
{
	if (!SetManifestIdentity(
		OutManifest,
		RPGGameplayTags::GameItem_Craft_fruit,
		EItemCategory::Craft) ||
		!AddStackFragment(OutManifest, MaxStackSize))
	{
		return false;
	}
	AddNameFragment(OutManifest, TEXT("Legacy Fruit"));
	return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGLegacyItemDefinitionAdapterTest,
	"ProjectRPG.Item.Legacy.AdapterBuildsStableDefinitionView",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGLegacyItemDefinitionAdapterTest::RunTest(
	const FString& Parameters)
{
	using namespace RPGLegacyItemDefinitionAdapterTests;
	FItemManifest Manifest;
	TestTrue(
		TEXT("A representative legacy manifest can be authored"),
		MakeLegacyManifest(Manifest));

	FRPGItemDefinitionView View;
	FString Error;
	TestTrue(
		TEXT("The legacy manifest produces a definition view"),
		FRPGLegacyItemDefinitionAdapter::TryBuildDefinitionView(
			Manifest,
			View,
			&Error));
	TestTrue(TEXT("The resulting view is valid"), View.IsValid());
	TestEqual(
		TEXT("The stable definition name is the item tag"),
		View.Snapshot.DefinitionId.PrimaryAssetName,
		RPGGameplayTags::GameItem_Craft_fruit.GetTag().GetTagName());
	TestEqual(
		TEXT("The legacy definition starts at version one"),
		View.Snapshot.DefinitionVersion,
		1);
	TestEqual(
		TEXT("The authored maximum stack is retained"),
		View.Snapshot.MaxStackSize,
		37);
	TestTrue(TEXT("The source is marked as legacy"), View.bLegacySource);
	TestTrue(
		TEXT("The tagged name fragment becomes presentation data"),
		View.DisplayName.EqualTo(FText::FromString(TEXT("Legacy Fruit"))));

	URPGItemDefinition* NativeDefinition =
		NewObject<URPGItemDefinition>();
	NativeDefinition->ItemTag = RPGGameplayTags::GameItem_Craft_fruit;
	TestEqual(
		TEXT("A native replacement uses the same stable identity"),
		NativeDefinition->GetPrimaryAssetId(),
		View.Snapshot.DefinitionId);

	FItemManifest InvalidManifest;
	TestFalse(
		TEXT("A manifest without identity is rejected"),
		FRPGLegacyItemDefinitionAdapter::TryBuildDefinitionView(
			InvalidManifest,
			View,
			&Error));
	TestFalse(
		TEXT("A rejected manifest leaves no usable view"),
		View.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGLegacyItemDefinitionRegistryPrecedenceTest,
	"ProjectRPG.Item.Legacy.NativeDefinitionOverridesLegacyView",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGLegacyItemDefinitionRegistryPrecedenceTest::RunTest(
	const FString& Parameters)
{
	using namespace RPGLegacyItemDefinitionAdapterTests;
	FItemManifest Manifest;
	TestTrue(TEXT("The legacy manifest is valid"), MakeLegacyManifest(Manifest));
	FRPGItemDefinitionView LegacyView;
	TestTrue(
		TEXT("The legacy view can be built"),
		FRPGLegacyItemDefinitionAdapter::TryBuildDefinitionView(
			Manifest,
			LegacyView));

	FRPGItemDefinitionRegistry Registry;
	TestTrue(
		TEXT("The legacy view registers"),
		Registry.RegisterDefinitionView(LegacyView));
	FRPGItemDefinitionSnapshot Snapshot;
	TestTrue(
		TEXT("The transaction catalog exposes the legacy snapshot"),
		Registry.TryFindDefinition(
			LegacyView.Snapshot.DefinitionId,
			Snapshot));
	TestEqual(TEXT("The legacy stack rule is visible"), Snapshot.MaxStackSize, 37);

	URPGItemDefinition* NativeDefinition =
		NewObject<URPGItemDefinition>();
	NativeDefinition->ItemTag = RPGGameplayTags::GameItem_Craft_fruit;
	NativeDefinition->ItemCategory = EItemCategory::Craft;
	NativeDefinition->DisplayName = FText::FromString(TEXT("Native Fruit"));
	NativeDefinition->MaxStackSize = 12;
	TestTrue(
		TEXT("The native replacement registers"),
		Registry.RegisterDefinition(*NativeDefinition));
	TestTrue(
		TEXT("A later legacy scan remains a valid no-op"),
		Registry.RegisterDefinitionView(LegacyView));

	FRPGItemDefinitionView ResolvedView;
	TestTrue(
		TEXT("The presentation catalog resolves the replacement"),
		Registry.TryFindDefinitionView(
			NativeDefinition->GetPrimaryAssetId(),
			ResolvedView));
	TestFalse(TEXT("The native view wins over legacy data"), ResolvedView.bLegacySource);
	TestTrue(
		TEXT("Native presentation is retained"),
		ResolvedView.DisplayName.EqualTo(
			FText::FromString(TEXT("Native Fruit"))));
	TestTrue(
		TEXT("The authoritative snapshot still resolves"),
		Registry.TryFindDefinition(
			NativeDefinition->GetPrimaryAssetId(),
			Snapshot));
	TestEqual(TEXT("The native stack rule wins"), Snapshot.MaxStackSize, 12);

	Snapshot.DefinitionVersion = 2;
	Snapshot.MaxStackSize = 14;
	TestTrue(
		TEXT("A startup snapshot refresh registers"),
		Registry.RegisterSnapshot(Snapshot));
	TestTrue(
		TEXT("The refreshed presentation still resolves"),
		Registry.TryFindDefinitionView(
			NativeDefinition->GetPrimaryAssetId(),
			ResolvedView));
	TestEqual(
		TEXT("Authority and presentation retain one version"),
		ResolvedView.Snapshot.DefinitionVersion,
		2);
	TestEqual(
		TEXT("Authority and presentation retain one stack rule"),
		ResolvedView.Snapshot.MaxStackSize,
		14);
	return true;
}

#endif
