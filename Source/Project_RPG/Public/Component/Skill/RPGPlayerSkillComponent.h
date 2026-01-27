// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/PawnExtensionComponentBase.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "RPGPlayerSkillComponent.generated.h"

class URPGSkillDefinition;

/**
 * URPGPlayerSkillComponent
 * 
 * 유저의 스킬 포인트(SP), 스킬 레벨, 트라이포드 선택 정보를 관리합니다.
 * 로스트아크의 스킬창 시스템을 뒷받침하는 핵심 데이터 컴포넌트입니다.
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGPlayerSkillComponent : public UPawnExtensionComponentBase
{
	GENERATED_BODY()

public:
	URPGPlayerSkillComponent();

	// ---------------------------------------------------
	// 1. 스킬 관리 API
	// ---------------------------------------------------

	/** 스킬 레벨업 시도 (SP 체크 및 레벨업 로직) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool TryLevelUpSkill(FGameplayTag SkillTag);

	/** 스킬 레벨다운 시도 (SP 환급) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool TryLevelDownSkill(FGameplayTag SkillTag);

	/** [편의기능] 가능한 최대치(또는 10레벨)까지 한 번에 레벨업 (Shift + 클릭) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void LevelUpToMax(FGameplayTag SkillTag, int32 TargetGoalLevel = 10);

	/** [편의기능] 스킬 레벨을 1로 초기화하고 모든 SP 환급 (Shift + 우클릭 등) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void ResetSkillLevel(FGameplayTag SkillTag);

	/** 트라이포드 선택 (레벨 제한 체크 포함) */
	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	bool SelectTripod(FGameplayTag SkillTag, int32 TierIndex, int32 OptionIndex);

	/** 특정 스킬의 저장된 데이터 가져오기 */
	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	FRPGSkillSaveData GetSkillSaveData(FGameplayTag SkillTag) const;

	// ---------------------------------------------------
	// 2. 스킬 포인트(SP) 관리
	// ---------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	int32 GetRemainingSP() const { return TotalSP - UsedSP; }

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void AddTotalSP(int32 Amount) { TotalSP += Amount; }

protected:
	// 스킬 레벨에 따른 소모 SP 계산 (로아 방식: 고레벨일수록 많이 필요)
	int32 GetRequiredSPForLevel(int32 TargetLevel) const;

private:
	// 유저의 스킬 정보 맵 (태그 -> 데이터)
	UPROPERTY()
	TMap<FGameplayTag, FRPGSkillSaveData> SkillDataMap;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Skill")
	int32 TotalSP = 50; // 기본 지급 SP

	int32 UsedSP = 0;
};