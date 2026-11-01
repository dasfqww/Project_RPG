#include "Experience/RPGCreateExperienceAssetsCommandlet.h"

#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "UObject/UnrealType.h"
#include "DataAsset/Definition/RPGExperienceActionSet.h"
#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "Factories/DataAssetFactory.h"
#include "GameFeatureAction_AddComponents.h"
#include "GameFeatureData.h"
#include "GameState/RPGGameStateBase.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGExperienceAssets, Log, All);

namespace
{
	template<typename AssetType>
	AssetType* CreateMissingAsset(const FString& PackageName, bool& bCreated)
	{
		bCreated = false;
		const FString Name = FPackageName::GetShortName(PackageName);
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UE_LOG(LogRPGExperienceAssets, Display, TEXT("Preserving %s"), *PackageName);
			return LoadObject<AssetType>(nullptr, *(PackageName + TEXT(".") + Name));
		}
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = AssetType::StaticClass();
		IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		AssetType* Asset = Cast<AssetType>(Tools.CreateAsset(
			Name, FPackageName::GetLongPackagePath(PackageName), AssetType::StaticClass(), Factory));
		bCreated = Asset != nullptr;
		return Asset;
	}

	bool SaveNewAsset(UObject* Asset)
	{
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args);
	}
}

URPGCreateExperienceAssetsCommandlet::URPGCreateExperienceAssetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URPGCreateExperienceAssetsCommandlet::Main(const FString& Params)
{
	bool bCreated = false;
	UGameFeatureData* Feature = CreateMissingAsset<UGameFeatureData>(
		TEXT("/RPGClientPresentation/RPGClientPresentation"), bCreated);
	if (!Feature || (bCreated && !SaveNewAsset(Feature)))
	{
		UE_LOG(LogRPGExperienceAssets, Error, TEXT("Could not create presentation GameFeatureData."));
		return 1;
	}

	URPGExperienceActionSet* Set = CreateMissingAsset<URPGExperienceActionSet>(
		TEXT("/Game/System/Experiences/ActionSets/DA_ClientPresentation"), bCreated);
	if (!Set) { return 1; }
	if (bCreated)
	{
		Set->GameFeaturesToEnable.Add(TEXT("RPGClientPresentation"));
		UGameFeatureAction_AddComponents* Action =
			NewObject<UGameFeatureAction_AddComponents>(Set, NAME_None, RF_Transactional);
		FGameFeatureComponentEntry Entry;
		Entry.ActorClass = ARPGGameStateBase::StaticClass();
		Entry.ComponentClass = TSoftClassPtr<UActorComponent>(FSoftObjectPath(
			TEXT("/Script/RPGClientPresentationRuntime.RPGClientPresentationComponent")));
		Entry.bClientComponent = true;
		Entry.bServerComponent = false;
		Action->ComponentList.Add(Entry);
		Set->Actions.Add(Action);
		if (!SaveNewAsset(Set)) { return 1; }
	}

	URPGExperienceDefinition* Experience = CreateMissingAsset<URPGExperienceDefinition>(
		TEXT("/Game/System/Experiences/DA_ClientPresentation"), bCreated);
	if (!Experience) { return 1; }
	if (bCreated)
	{
		Experience->ActionSets.Add(Set);
		if (!SaveNewAsset(Experience)) { return 1; }
	}

	UGameFeatureData* OptionsFeature = CreateMissingAsset<UGameFeatureData>(
		TEXT("/RPGOptionsUI/RPGOptionsUI"), bCreated);
	if (!OptionsFeature || (bCreated && !SaveNewAsset(OptionsFeature))) { return 1; }

	URPGExperienceActionSet* OptionsSet = CreateMissingAsset<URPGExperienceActionSet>(
		TEXT("/Game/System/Experiences/ActionSets/DA_OptionsUI"), bCreated);
	if (!OptionsSet) { return 1; }
	if (bCreated)
	{
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
		UClass* ActionClass = LoadClass<UGameFeatureAction>(nullptr,
			TEXT("/Script/RPGOptionsUIRuntime.GameFeatureAction_RPGOptionsUI"));
		if (!ActionClass)
		{
			UE_LOG(LogRPGExperienceAssets, Error, TEXT("Could not load options GameFeature action."));
			return 1;
		}
		OptionsSet->GameFeaturesToEnable.Add(TEXT("RPGOptionsUI"));
		OptionsSet->Actions.Add(NewObject<UGameFeatureAction>(
			OptionsSet, ActionClass, NAME_None, RF_Transactional));
		if (!SaveNewAsset(OptionsSet)) { return 1; }
	}

	URPGExperienceDefinition* Composed = CreateMissingAsset<URPGExperienceDefinition>(
		TEXT("/Game/System/Experiences/DA_ClientPresentationAndOptions"), bCreated);
	if (!Composed) { return 1; }
	if (bCreated)
	{
		Composed->ActionSets.Add(Set);
		Composed->ActionSets.Add(OptionsSet);
		if (!SaveNewAsset(Composed)) { return 1; }
	}
	UGameFeatureData* InputFeature = CreateMissingAsset<UGameFeatureData>(
		TEXT("/RPGInputMapping/RPGInputMapping"), bCreated);
	if (!InputFeature || (bCreated && !SaveNewAsset(InputFeature))) { return 1; }

	UInputAction* ToggleAction = CreateMissingAsset<UInputAction>(
		TEXT("/RPGInputMapping/Input/IA_OptionsShortcut"), bCreated);
	if (!ToggleAction) { return 1; }
	if (bCreated)
	{
		ToggleAction->ValueType = EInputActionValueType::Boolean;
		if (!SaveNewAsset(ToggleAction)) { return 1; }
	}
	UInputMappingContext* ShortcutMapping = CreateMissingAsset<UInputMappingContext>(
		TEXT("/RPGInputMapping/Input/IMC_OptionsShortcut"), bCreated);
	if (!ShortcutMapping) { return 1; }
	if (bCreated)
	{
		FEnumProperty* Tracking = FindFProperty<FEnumProperty>(
			ShortcutMapping->GetClass(), TEXT("RegistrationTrackingMode"));
		if (!Tracking) { return 1; }
		Tracking->GetUnderlyingProperty()->SetIntPropertyValue(
			Tracking->ContainerPtrToValuePtr<void>(ShortcutMapping),
			static_cast<int64>(EMappingContextRegistrationTrackingMode::CountRegistrations));
		ShortcutMapping->MapKey(ToggleAction, EKeys::F10);
		if (!SaveNewAsset(ShortcutMapping)) { return 1; }
	}

	URPGExperienceActionSet* ShortcutSet = CreateMissingAsset<URPGExperienceActionSet>(
		TEXT("/Game/System/Experiences/ActionSets/DA_OptionsShortcut"), bCreated);
	if (!ShortcutSet) { return 1; }
	if (bCreated)
	{
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGInputMappingRuntime"));
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
		UClass* MappingClass = LoadClass<UGameFeatureAction>(nullptr,
			TEXT("/Script/RPGInputMappingRuntime.GameFeatureAction_RPGInputMapping"));
		UClass* OptionsClass = LoadClass<UGameFeatureAction>(nullptr,
			TEXT("/Script/RPGOptionsUIRuntime.GameFeatureAction_RPGOptionsUI"));
		if (!MappingClass || !OptionsClass) { return 1; }
		UGameFeatureAction* MappingAction = NewObject<UGameFeatureAction>(
			ShortcutSet, MappingClass, NAME_None, RF_Transactional);
		UGameFeatureAction* OptionsAction = NewObject<UGameFeatureAction>(
			ShortcutSet, OptionsClass, NAME_None, RF_Transactional);
		FArrayProperty* Entries = FindFProperty<FArrayProperty>(MappingClass, TEXT("InputMappings"));
		FStructProperty* EntryType = Entries ? CastField<FStructProperty>(Entries->Inner) : nullptr;
		FSoftObjectProperty* MappingProperty = EntryType
			? FindFProperty<FSoftObjectProperty>(EntryType->Struct, TEXT("MappingContext")) : nullptr;
		FSoftObjectProperty* ToggleProperty = FindFProperty<FSoftObjectProperty>(OptionsClass, TEXT("ToggleInputAction"));
		if (!Entries || !MappingProperty || !ToggleProperty) { return 1; }
		FScriptArrayHelper Helper(Entries, Entries->ContainerPtrToValuePtr<void>(MappingAction));
		const int32 EntryIndex = Helper.AddValue();
		MappingProperty->SetPropertyValue_InContainer(Helper.GetRawPtr(EntryIndex), FSoftObjectPtr(ShortcutMapping));
		ToggleProperty->SetPropertyValue_InContainer(OptionsAction, FSoftObjectPtr(ToggleAction));
		ShortcutSet->GameFeaturesToEnable.Add(TEXT("RPGInputMapping"));
		ShortcutSet->GameFeaturesToEnable.Add(TEXT("RPGOptionsUI"));
		ShortcutSet->Actions.Add(MappingAction);
		ShortcutSet->Actions.Add(OptionsAction);
		if (!SaveNewAsset(ShortcutSet)) { return 1; }
	}
	URPGExperienceDefinition* ShortcutExperience = CreateMissingAsset<URPGExperienceDefinition>(
		TEXT("/Game/System/Experiences/DA_ClientPresentationAndOptionsShortcut"), bCreated);
	if (!ShortcutExperience) { return 1; }
	if (bCreated)
	{
		ShortcutExperience->ActionSets.Add(Set);
		ShortcutExperience->ActionSets.Add(ShortcutSet);
		if (!SaveNewAsset(ShortcutExperience)) { return 1; }
	}
	UGameFeatureData* ClientUIFeature = CreateMissingAsset<UGameFeatureData>(
		TEXT("/RPGClientUI/RPGClientUI"), bCreated);
	if (!ClientUIFeature || (bCreated && !SaveNewAsset(ClientUIFeature))) { return 1; }

	URPGExperienceActionSet* HintSet = CreateMissingAsset<URPGExperienceActionSet>(
		TEXT("/Game/System/Experiences/ActionSets/DA_OptionsShortcutHint"), bCreated);
	if (!HintSet) { return 1; }
	if (bCreated)
	{
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGClientUIRuntime"));
		FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
		UClass* ClientWidgetClass = LoadClass<UGameFeatureAction>(nullptr,
			TEXT("/Script/RPGClientUIRuntime.GameFeatureAction_RPGClientWidgets"));
		UClass* HintClass = LoadClass<UUserWidget>(nullptr,
			TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint"));
		if (!ClientWidgetClass || !HintClass) { return 1; }
		UGameFeatureAction* WidgetAction = NewObject<UGameFeatureAction>(
			HintSet, ClientWidgetClass, NAME_None, RF_Transactional);
		FArrayProperty* WidgetEntries = FindFProperty<FArrayProperty>(ClientWidgetClass, TEXT("Widgets"));
		FStructProperty* WidgetEntryType = WidgetEntries ? CastField<FStructProperty>(WidgetEntries->Inner) : nullptr;
		FNameProperty* WidgetIdProperty = WidgetEntryType
			? FindFProperty<FNameProperty>(WidgetEntryType->Struct, TEXT("WidgetId")) : nullptr;
		FSoftClassProperty* ClassProperty = WidgetEntryType
			? FindFProperty<FSoftClassProperty>(WidgetEntryType->Struct, TEXT("WidgetClass")) : nullptr;
		if (!WidgetEntries || !WidgetIdProperty || !ClassProperty) { return 1; }
		FScriptArrayHelper WidgetHelper(WidgetEntries, WidgetEntries->ContainerPtrToValuePtr<void>(WidgetAction));
		const int32 WidgetIndex = WidgetHelper.AddValue();
		WidgetIdProperty->SetPropertyValue_InContainer(WidgetHelper.GetRawPtr(WidgetIndex), FName(TEXT("OptionsShortcutHint")));
		ClassProperty->SetPropertyValue_InContainer(WidgetHelper.GetRawPtr(WidgetIndex), FSoftObjectPtr(HintClass));
		HintSet->GameFeaturesToEnable.Add(TEXT("RPGClientUI"));
		HintSet->GameFeaturesToEnable.Add(TEXT("RPGOptionsUI"));
		HintSet->Actions.Add(WidgetAction);
		if (!SaveNewAsset(HintSet)) { return 1; }
	}
	URPGExperienceDefinition* HintExperience = CreateMissingAsset<URPGExperienceDefinition>(
		TEXT("/Game/System/Experiences/DA_ClientPresentationAndOptionsHint"), bCreated);
	if (!HintExperience) { return 1; }
	if (bCreated)
	{
		HintExperience->ActionSets.Add(Set);
		HintExperience->ActionSets.Add(ShortcutSet);
		HintExperience->ActionSets.Add(HintSet);
		if (!SaveNewAsset(HintExperience)) { return 1; }
	}
	UE_LOG(LogRPGExperienceAssets, Display,
		TEXT("RPG_EXPERIENCE ASSETS_READY Presentation, OptionsUI, OptionsShortcut, OptionsHint"));
	return 0;
}
