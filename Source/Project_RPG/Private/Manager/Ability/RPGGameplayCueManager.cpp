#include "Manager/Ability/RPGGameplayCueManager.h"
#include "AbilitySystemGlobals.h"
#include "GameplayTagsManager.h"
#include "Engine/World.h"

URPGGameplayCueManager::URPGGameplayCueManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

URPGGameplayCueManager* URPGGameplayCueManager::Get()
{
	return Cast<URPGGameplayCueManager>(UAbilitySystemGlobals::Get().GetGameplayCueManager());
}

void URPGGameplayCueManager::OnCreated()
{
	Super::OnCreated();
	UpdateDelayLoadDelegateListeners();
}

bool URPGGameplayCueManager::ShouldAsyncLoadRuntimeObjectLibraries() const
{
	// 런타임에는 게임플레이 큐를 비동기로 로드합니다
	return !ShouldDelayLoadGameplayCues();
}

bool URPGGameplayCueManager::ShouldSyncLoadMissingGameplayCues() const
{
	// 누락된 게임플레이 큐의 동기 로드는 비활성화
	return false;
}

bool URPGGameplayCueManager::ShouldAsyncLoadMissingGameplayCues() const
{
	// 누락된 게임플레이 큐의 비동기 로드는 활성화
	return true;
}

void URPGGameplayCueManager::OnGameplayTagLoaded(const FGameplayTag& Tag)
{
	// 게임플레이 태그가 로드될 때의 처리 로직
	// 필요시 확장 가능
}

void URPGGameplayCueManager::HandlePostGarbageCollect()
{
	// 가비지 컬렉션 후의 처리 로직
	// 필요시 확장 가능
}

void URPGGameplayCueManager::HandlePostLoadMap(UWorld* NewWorld)
{
	// 맵 로드 후의 처리 로직
	// 필요시 확장 가능
}

void URPGGameplayCueManager::UpdateDelayLoadDelegateListeners()
{
	// 델리게이트 리스너 업데이트
	UGameplayTagsManager::Get().OnGameplayTagLoadedDelegate.RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	if (!ShouldDelayLoadGameplayCues())
	{
		return;
	}

	// 지연 로드가 필요한 경우 델리게이트 등록
	UGameplayTagsManager::Get().OnGameplayTagLoadedDelegate.AddUObject(this, &URPGGameplayCueManager::OnGameplayTagLoaded);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &URPGGameplayCueManager::HandlePostGarbageCollect);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URPGGameplayCueManager::HandlePostLoadMap);
}

bool URPGGameplayCueManager::ShouldDelayLoadGameplayCues() const
{
	// 클라이언트에서만 지연 로드 사용
	return !IsRunningDedicatedServer();
}
