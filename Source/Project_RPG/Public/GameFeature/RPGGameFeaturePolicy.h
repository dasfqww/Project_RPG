// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFeaturesProjectPolicies.h"
#include "RPGGameFeaturePolicy.generated.h"

class UGameFeatureData;

/**
 * URPGGameFeaturePolicy
 * 
 * RPG 프로젝트용 게임 기능 정책
 * - 스킬/이펙트 등 게임플레이 큐 경로에 중점
 * - 불필요한 플러그인/핫픽스/스플릿스크린 로직 제거
 * - RPG 프로젝트에 최적화된 단순 구조
 */
UCLASS()
class PROJECT_RPG_API URPGGameFeaturePolicy : public UDefaultGameFeaturesProjectPolicies
{
	GENERATED_BODY()

public:
	/** 싱글톤 인스턴스 반환 */
	static URPGGameFeaturePolicy& Get();

	URPGGameFeaturePolicy(const FObjectInitializer& ObjectInitializer);

	//~UGameFeaturesProjectPolicies interface
	/** 게임 기능 매니저 초기화 */
	virtual void InitGameFeatureManager() override;

	/** 게임 기능 매니저 종료 */
	virtual void ShutdownGameFeatureManager() override;

	/** 게임 기능별 사전 로드 에셋 목록 반환 */
	virtual TArray<FPrimaryAssetId> GetPreloadAssetListForGameFeature(
		const UGameFeatureData* GameFeatureToLoad,
		bool bIncludeLoadedAssets = false) const override;

	/** 클라이언트/서버 데이터 로드 모드 설정 */
	virtual void GetGameFeatureLoadingMode(
		bool& bLoadClientData,
		bool& bLoadServerData) const override;
	//~End of UGameFeaturesProjectPolicies interface

private:
	/** 게임 기능 변경 시 알림을 받을 옵저버들 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Observers;
};
