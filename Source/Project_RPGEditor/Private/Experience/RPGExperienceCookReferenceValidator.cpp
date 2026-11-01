#include "Experience/RPGExperienceCookReferenceValidator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAsset/Definition/RPGExperienceActionSet.h"
#include "DataAsset/Definition/RPGExperienceDefinition.h"
#include "DataAsset/Definition/RPGUserFacingExperienceDefinition.h"
#include "Engine/AssetManager.h"
#include "Misc/DataValidation.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Settings/ProjectPackagingSettings.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGExperienceCookReferenceValidator)

#define LOCTEXT_NAMESPACE "RPGExperienceCookReferences"

namespace
{
	TArray<FString> GetExcludedDirectories()
	{
		TArray<FString> Directories;
		for (const FDirectoryPath& Entry : GetDefault<UProjectPackagingSettings>()->DirectoriesToNeverCook)
		{
			FString LocalPath, PackagePath;
			// Use the same game-relative/package path conversion as CookByTheBook.
			if (!FPackageName::TryConvertGameRelativePackagePathToLocalPath(Entry.Path, LocalPath))
			{
				continue;
			}
			FPaths::NormalizeDirectoryName(LocalPath);
			FPaths::CollapseRelativeDirectories(LocalPath);
			if (FPackageName::TryConvertFilenameToLongPackageName(LocalPath, PackagePath))
			{
				while (PackagePath.RemoveFromEnd(TEXT("/"))) {}
				if (!PackagePath.IsEmpty()) { Directories.AddUnique(PackagePath); }
			}
		}
		return Directories;
	}

	const FString* FindExcludedDirectory(FName Package, const TArray<FString>& Directories)
	{
		const FString Name = Package.ToString();
		return Directories.FindByPredicate([&Name](const FString& Directory)
		{
			return Name.Equals(Directory, ESearchCase::IgnoreCase)
				|| Name.StartsWith(Directory + TEXT("/"), ESearchCase::IgnoreCase);
		});
	}

	FString MakeReferenceChain(FName Package, const TMap<FName, FName>& Parents)
	{
		TArray<FString> Chain;
		for (FName Current = Package; !Current.IsNone(); Current = Parents.FindChecked(Current))
		{
			Chain.Add(Current.ToString());
		}
		FString Result;
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
		{
			if (!Result.IsEmpty()) { Result += TEXT(" -> "); }
			Result += Chain[Index];
		}
		return Result;
	}
}

bool URPGExperienceCookReferenceValidator::CanValidateAsset_Implementation(
	const FAssetData& AssetData, UObject* Asset, FDataValidationContext& Context) const
{
	if (!IsValid(Asset) || !AssetData.IsValid()
		|| (!Asset->IsA<URPGExperienceDefinition>() && !Asset->IsA<URPGExperienceActionSet>()
			&& !Asset->IsA<URPGUserFacingExperienceDefinition>()))
	{
		return false;
	}
	const TArray<FString> Directories = GetExcludedDirectories();
	if (Directories.IsEmpty() || FindExcludedDirectory(AssetData.PackageName, Directories))
	{
		return false;
	}
	// Asset Registry dependency queries describe saved packages, not unsaved object changes.
	if (Asset->GetPackage()->IsDirty())
	{
		Context.AddWarning(FText::Format(
			LOCTEXT("UnsavedRoot", "{0}: cook reference validation was skipped. Save the asset and validate again; dependency data describes the last saved version."),
			FText::FromName(AssetData.PackageName)));
		return false;
	}
	if (FAssetRegistryModule::GetRegistry().IsLoadingAssets())
	{
		Context.AddWarning(LOCTEXT("RegistryBusy",
			"Cook reference validation was skipped while the Asset Registry is scanning. Validate again after the scan finishes."));
		return false;
	}
	return true;
}

EDataValidationResult URPGExperienceCookReferenceValidator::ValidateLoadedAsset_Implementation(
	const FAssetData& AssetData, UObject* Asset, FDataValidationContext& Context)
{
	IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
	const TArray<FString> Directories = GetExcludedDirectories();
	const UE::AssetRegistry::FDependencyQuery GameDependencies(UE::AssetRegistry::EDependencyQuery::Game);
	TArray<FName> Pending = {AssetData.PackageName};
	TMap<FName, FName> Parents;
	Parents.Add(AssetData.PackageName, NAME_None);
	bool bFailed = false;

	// Primary asset IDs are authored data, not package references; follow them explicitly.
	if (const URPGUserFacingExperienceDefinition* Frontend = Cast<URPGUserFacingExperienceDefinition>(Asset))
	{
		const UAssetManager* Manager = UAssetManager::GetIfInitialized();
		const auto AddSelection = [&](const FPrimaryAssetId& Id, const TCHAR* FieldName)
		{
			if (!Id.IsValid()) { return; } // Structural errors are reported by the definition's own validation.
			const FSoftObjectPath Path = Manager ? Manager->GetPrimaryAssetPath(Id) : FSoftObjectPath();
			if (!Path.IsValid())
			{
				AssetFails(Asset, FText::Format(
					LOCTEXT("UnregisteredFrontendSelection",
						"{0} ID {1} has no registered package. Fix the ID or Asset Manager scan settings and validate again."),
					FText::FromString(FieldName), FText::FromString(Id.ToString())));
				bFailed = true;
				return;
			}
			const FName SelectedPackage(*Path.GetLongPackageName());
			if (!Parents.Contains(SelectedPackage))
			{
				Parents.Add(SelectedPackage, AssetData.PackageName);
				Pending.Add(SelectedPackage);
			}
		};
		AddSelection(Frontend->MapID, TEXT("MapID"));
		AddSelection(Frontend->ExperienceID, TEXT("ExperienceID"));
	}

	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		// Copy before Pending can grow; shared references and cycles are visited once.
		const FName Package = Pending[Index];
		if (const FString* Directory = FindExcludedDirectory(Package, Directories))
		{
			AssetFails(Asset, FText::Format(
				LOCTEXT("ExcludedReference",
					"Saved game dependency reaches Packaging's excluded directory {0}. Reference chain: {1}. Use runtime assets outside excluded directories."),
				FText::FromString(*Directory), FText::FromString(MakeReferenceChain(Package, Parents))));
			bFailed = true;
			continue;
		}
		if (const UPackage* Loaded = FindPackage(nullptr, *Package.ToString()); Loaded && Loaded->IsDirty())
		{
			AssetFails(Asset, FText::Format(
				LOCTEXT("UnsavedDependency",
					"Save changed dependency {0} and validate again. Saved reference chain: {1}."),
				FText::FromName(Package), FText::FromString(MakeReferenceChain(Package, Parents))));
			bFailed = true;
			continue;
		}
		TArray<FName> Dependencies;
		if (!Registry.GetDependencies(Package, Dependencies, UE::AssetRegistry::EDependencyCategory::Package,
			GameDependencies))
		{
			AssetFails(Asset, FText::Format(
				LOCTEXT("MissingDependencyData",
					"Saved dependency data is unavailable for {0}. Scan or mount its content and validate again. Reference chain: {1}."),
				FText::FromName(Package), FText::FromString(MakeReferenceChain(Package, Parents))));
			bFailed = true;
			continue;
		}
		Dependencies.Sort(FNameLexicalLess());
		for (FName Dependency : Dependencies)
		{
			// Script packages contain native types; this check follows content packages only.
			if (Dependency.IsNone() || Dependency.ToString().StartsWith(TEXT("/Script/"))
				|| Parents.Contains(Dependency))
			{
				continue;
			}
			Parents.Add(Dependency, Package);
			Pending.Add(Dependency);
		}
	}
	if (bFailed) { return EDataValidationResult::Invalid; }
	AssetPasses(Asset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
