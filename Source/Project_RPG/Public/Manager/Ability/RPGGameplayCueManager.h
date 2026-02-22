#pragma once

#include "GameplayCueManager.h"
#include "RPGGameplayCueManager.generated.h"

class UGameFeatureData;

/**
 * URPGGameplayCueManager
 * 
 * RPG 프로젝트용 게임플레이 큐 관리자
 * 게임플레이 큐의 로딩과 캐싱을 관리합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGGameplayCueManager : public UGameplayCueManager
{
	GENERATED_BODY()

public:
	URPGGameplayCueManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static URPGGameplayCueManager* Get();

	//~UGameplayCueManager interface
	virtual void OnCreated() override;
	virtual bool ShouldAsyncLoadRuntimeObjectLibraries() const override;
	virtual bool ShouldSyncLoadMissingGameplayCues() const override;
	virtual bool ShouldAsyncLoadMissingGameplayCues() const override;
	//~End of UGameplayCueManager interface

protected:
	/** 게임플레이 태그가 로드될 때 호출 */
	void OnGameplayTagLoaded(const FGameplayTag& Tag);

	/** 가비지 컬렉션 후 호출 */
	void HandlePostGarbageCollect();

	/** 맵 로드 후 호출 */
	void HandlePostLoadMap(UWorld* NewWorld);

	/** 지연 로드 델리게이트 리스너 업데이트 */
	void UpdateDelayLoadDelegateListeners();

	/** 게임플레이 큐 지연 로드 여부 반환 */
	bool ShouldDelayLoadGameplayCues() const;

private:
	/** 캐시된 게임플레이 큐 클래스들 */
	UPROPERTY(transient)
	TSet<TObjectPtr<UClass>> CachedGameplayCues;
};
