// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "RPGSkillConfig.generated.h"

class URPGGameplayAbility;
class UTexture2D;
class UAnimMontage;
class UNiagaraSystem;

/**
 * 로스트아크식 스킬 정보를 담는 데이터 에셋
 */
// Legacy prototype retained for existing assets. New skills use
// URPGSkillDefinition as the canonical definition.
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGSkillConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	FText SkillName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	TSoftObjectPtr<UTexture2D> SkillIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "General")
	TSubclassOf<URPGGameplayAbility> AbilityClass;

	/** 기본 전투 수치 (트라이포드 미적용 시) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BaseStats")
	float BaseCooldownTime;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BaseStats")
	float BaseManaCost;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BaseStats")
	float BaseIdentityGain; 

	/** 기본 연출 데이터 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BaseVisual")
	TObjectPtr<UAnimMontage> DefaultMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BaseVisual")
	TObjectPtr<UNiagaraSystem> DefaultVFX;

	/** 트라이포드 설정 (로아와 동일하게 3티어 구조) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tripods")
	TArray<FRPGSkillTripodOption> Tier1Options;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tripods")
	TArray<FRPGSkillTripodOption> Tier2Options;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tripods")
	TArray<FRPGSkillTripodOption> Tier3Options;
};
