// Fill out your copyright notice in the Description page of Project Settings.

#include "GameFeature/RPGGameFeaturePolicy.h"
#include "GameFeaturesSubsystem.h"

URPGGameFeaturePolicy& URPGGameFeaturePolicy::Get()
{
	return *GetMutableDefault<URPGGameFeaturePolicy>();
}

URPGGameFeaturePolicy::URPGGameFeaturePolicy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPGGameFeaturePolicy::InitGameFeatureManager()
{
	Super::InitGameFeatureManager();
	
	// RPG 프로젝트 특화 초기화 로직
	// - 게임플레이 큐 매니저는 DefaultEngine.ini에서 설정
	// - 추가 초기화가 필요하면 여기에 추가
	
	UE_LOG(LogGameFeatures, Log, TEXT("RPGGameFeaturePolicy initialized"));
}

void URPGGameFeaturePolicy::ShutdownGameFeatureManager()
{
	Super::ShutdownGameFeatureManager();
	
	// RPG 프로젝트 특화 종료 로직
	Observers.Empty();
	
	UE_LOG(LogGameFeatures, Log, TEXT("RPGGameFeaturePolicy shutdown complete"));
}

TArray<FPrimaryAssetId> URPGGameFeaturePolicy::GetPreloadAssetListForGameFeature(
	const UGameFeatureData* GameFeatureToLoad,
	bool bIncludeLoadedAssets) const
{
	// 게임 기능별로 사전 로드할 에셋이 필요한 경우 여기에 추가
	// 예: 스킬 데이터, 캐릭터 데이터 등
	return TArray<FPrimaryAssetId>();
}

void URPGGameFeaturePolicy::GetGameFeatureLoadingMode(
	bool& bLoadClientData,
	bool& bLoadServerData) const
{
	// RPG 프로젝트의 클라이언트/서버 로드 모드 설정
	bLoadClientData = true;  // 항상 클라이언트 데이터 로드
	bLoadServerData = !IsRunningDedicatedServer();  // 전용 서버가 아닐 때만 서버 데이터 로드
}
