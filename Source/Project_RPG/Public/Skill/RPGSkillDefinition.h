// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"
#include "Engine/DataTable.h"
#include "Type/RPGStructTypes.h"
#include "RPGSkillDefinition.generated.h"

class URPGSkillAction;
class UAnimMontage;
class UNiagaraSystem;

/**
 * 상태에 따라 변경될 스킬 데이터를 정의하는 구조체
 */
USTRUCT(BlueprintType)
struct FSkillModeOverride
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FGameplayTag RequiredStateTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UTexture2D* NewIcon = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UAnimMontage* NewMontage = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<URPGSkillAction> NewActionClass = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float DamageMultiplier = 1.0f;
};

/**
 * 트라이포드 티어 그룹 (3-3-2 구조)
 */
USTRUCT(BlueprintType)
struct FRPGSkillTripodTier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TArray<FRPGSkillTripodOption> Options;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32 RequiredSkillLevel = 4;
};

/**
 * URPGSkillDefinition
 */
UCLASS(BlueprintType, Const)
class PROJECT_RPG_API URPGSkillDefinition : public UDataAsset
{
	GENERATED_BODY()
	
public:
	// 현재 캐릭터 상태와 선택된 트라이포드에 맞는 데이터를 반환
	void GetSkillDataForContext(AActor* InActor, const TArray<int32>& SelectedTripods, UTexture2D*& OutIcon, UAnimMontage*& OutMontage, TSubclassOf<URPGSkillAction>& OutActionClass) const;

	// ---------------------------------------------------
	// 1. 기본 설정 및 데이터 테이블 연동
	// ---------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	FDataTableRowHandle SkillDataHandle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	FText SkillName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	UTexture2D* SkillIcon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	FGameplayTag SkillTag;

	// ---------------------------------------------------
	// 2. 동작 로직 및 비주얼
	// ---------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Action")
	TSubclassOf<URPGSkillAction> DefaultActionClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UAnimMontage> SkillMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Visual")
	TObjectPtr<UNiagaraSystem> SkillVFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cooldown")
	float BaseCooldown = 5.0f;

	// ---------------------------------------------------
	// 3. 변이 및 트라이포드
	// ---------------------------------------------------

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Overrides")
	TArray<FSkillModeOverride> ModeOverrides;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tripods")
	TArray<FRPGSkillTripodTier> TripodTiers;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config")
	int32 MaxSkillLevel = 12;
};