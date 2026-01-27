#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Http.h"
#include "Type/RPGStructTypes.h"
#include "HttpWebManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySaved, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryLoaded, const TArray<FItemSaveData>&, LoadedData);

/**
 * 웹 서버와 통신하여 데이터를 저장/로드하는 매니저
 * GameInstanceSubsystem을 상속받아 게임 실행 중 항상 유지됩니다.
 */
UCLASS()
class PROJECT_RPG_API UHttpWebManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 인벤토리 데이터를 웹 서버로 전송 (Save)
	void SaveInventoryToWeb(const TArray<FItemSaveData>& InventoryData, const FString& CharacterID);

	// 웹 서버로부터 인벤토리 데이터 요청 (Load)
	void LoadInventoryFromWeb(const FString& CharacterID);

	// 웹 서버 응답 처리 콜백 (Save)
	void OnSaveInventoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	// 웹 서버 응답 처리 콜백 (Load)
	void OnLoadInventoryResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventorySaved OnInventorySaved;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventoryLoaded OnInventoryLoaded;

private:
	// 테스트용 로컬 주소 (나중에 실제 서버 주소로 변경 필요)
	FString ApiUrl = TEXT("http://localhost:3000/api");
};