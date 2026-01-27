// Fill out your copyright notice in the Description page of Project Settings.


#include "Manager/DataManager.h"
#include "JsonObjectConverter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Item/PickUp/RPGPickUpBase.h"

UDataManager* UDataManager::Instance = nullptr;

UDataManager::UDataManager()
{
}

void UDataManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Instance = this;
	
	// 초기화 시 아이템 캐시 구축
	RefreshItemCache();
	UE_LOG(LogTemp, Log, TEXT("DataManager Initialized and Item Cache Refreshed."));
}

UDataManager* UDataManager::Get()
{
	if (Instance) return Instance;

	UWorld* World = nullptr;
	if (GEngine && GEngine->GameViewport)
	{
		World = GEngine->GameViewport->GetWorld();
	}

	if (World)
	{
		UGameInstance* GI = World->GetGameInstance();
		if (GI)
		{
			return GI->GetSubsystem<UDataManager>();
		}
	}
	return nullptr;
}

void UDataManager::RefreshItemCache()
{
	ItemManifestCache.Empty();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> AssetData;

	// ARPGPickUpBase를 상속받은 모든 블루프린트 검색
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add("/Game");

	AssetRegistryModule.Get().GetAssets(Filter, AssetData);

	for (const FAssetData& Data : AssetData)
	{
		UBlueprint* BP = Cast<UBlueprint>(Data.GetAsset());
		if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(ARPGPickUpBase::StaticClass()))
		{
			ARPGPickUpBase* CDO = Cast<ARPGPickUpBase>(BP->GeneratedClass->GetDefaultObject());
			if (CDO)
			{
				const FItemManifest& Manifest = CDO->GetItemManifest();
				if (Manifest.GetItemTag().IsValid())
				{
					ItemManifestCache.Add(Manifest.GetItemTag(), Manifest);
				}
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("Item Cache Refreshed. Found %d items."), ItemManifestCache.Num());
}

bool UDataManager::GetItemManifestByTag(const FGameplayTag& Tag, FItemManifest& OutManifest)
{
	if (const FItemManifest* FoundManifest = ItemManifestCache.Find(Tag))
	{
		OutManifest = *FoundManifest;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("Could not find item manifest for tag: %s"), *Tag.ToString());
	return false;
}

bool UDataManager::LoadSoundOptionsFromJson(FSoundSaveData& OutData)
{
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("sound_options.json");
	FString JsonString;

	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> JsonObject;

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	//  
	OutData.MasterVolume = JsonObject->GetNumberField(TEXT("MasterVolume"));
	OutData.bMasterMuted = JsonObject->GetBoolField(TEXT("bMasterMuted"));

	// Volumes
	const TSharedPtr<FJsonObject>* VolumesObject;
	if (JsonObject->TryGetObjectField(TEXT("Volumes"), VolumesObject))
	{
		for (const auto& Elem : (*VolumesObject)->Values)
		{
			OutData.Volumes.Add(Elem.Key, (float)(Elem.Value->AsNumber()));
		}
	}

	// Mutes
	const TSharedPtr<FJsonObject>* MutesObject;
	if (JsonObject->TryGetObjectField(TEXT("Mutes"), MutesObject))
	{
		for (const auto& Elem : (*MutesObject)->Values)
		{
			OutData.Mutes.Add(Elem.Key, Elem.Value->AsBool());
		}
	}

	return true;
}

bool UDataManager::LoadGraphicOptionsFromJson(FGraphicSaveData& OutData)
{
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("graphic_options.json");
	FString JsonString;

	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> JsonObject;

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), &OutData))
	{
		return false;
	}

	return true;
}

// [삭제됨] LoadInventoryJson, SaveInventoryJson - 이제 DB를 사용하므로 더 이상 로컬 JSON 파일 처리를 하지 않습니다.
