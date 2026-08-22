// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Type/RPGStructTypes.h"
#include "JsonObjectConverter.h"
#include "Subsystems/GameInstanceSubsystem.h" // 추가
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "DataManager.generated.h"

class URPGItemDefinition;

/**
 *
 */
UCLASS()
class PROJECT_RPG_API UDataManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UDataManager();
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 서브시스템 방식으로 싱글톤 접근
	static UDataManager* Get();
	template<typename T>
	void SaveOptionToJson(const T& OptionData, const FString& FileName);

	bool LoadSoundOptionsFromJson(FSoundSaveData& OutData);

    bool LoadGraphicOptionsFromJson(FGraphicSaveData& OutData);

	/*bool LoadInventoryJson(TArray<FItemSaveData>& OutInventoryData);

	void SaveInventoryJson(const TArray<FItemSaveData>& InventoryData);*/

	// [NEW] 태그로 아이템 Manifest 데이터 찾기
	bool GetItemManifestByTag(const FGameplayTag& Tag, FItemManifest& OutManifest);

	const IRPGItemDefinitionCatalog& GetItemDefinitionCatalog() const
	{
		return ItemDefinitionRegistry;
	}

	const IRPGItemDefinitionViewCatalog& GetItemDefinitionViewCatalog() const
	{
		return ItemDefinitionRegistry;
	}

	const IRPGItemActionPolicyCatalog& GetItemActionPolicyCatalog() const
	{
		return ItemActionPolicyRegistry;
	}

	const URPGItemDefinition* FindNativeItemDefinition(
		const FPrimaryAssetId& DefinitionId) const;

	bool TryGetItemDefinitionViewByTag(
		const FGameplayTag& Tag,
		FRPGItemDefinitionView& OutView) const;
	
	// 전체 아이템 캐시 갱신 (블루프린트 검색)
	void RefreshItemCache();

	// [NEW] 아이템 데이터 테이블 (PickUp 클래스 정보 포함) - Legacy
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Data")
	TObjectPtr<UDataTable> ItemDataTable;

private:
	// 태그 - Manifest 매핑 캐시
	TMap<FGameplayTag, FItemManifest> ItemManifestCache;
	FRPGItemDefinitionRegistry ItemDefinitionRegistry;
	FRPGItemActionPolicyRegistry ItemActionPolicyRegistry;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<URPGItemDefinition>> NativeItemDefinitions;

	static UDataManager* Instance;

};

template<typename T>
inline void UDataManager::SaveOptionToJson(const T& OptionData, const FString& FileName)
{
    FString JsonString;
    if (FJsonObjectConverter::UStructToJsonObjectString(OptionData, JsonString))
    {
        const FString FullPath = FPaths::ProjectSavedDir() / FileName;

        if (FFileHelper::SaveStringToFile(JsonString, *FullPath))
        {
            UE_LOG(LogTemp, Log, TEXT("Successfully saved option to: %s"), *FullPath);
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("Failed to save option to: %s"), *FullPath);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to serialize option data."));
    }
}
