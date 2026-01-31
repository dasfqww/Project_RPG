// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "GameplayTagContainer.h"
#include "RPGSkillViewModel.generated.h"

class URPGPlayerSkillComponent;
class URPGSkillSlotViewModel;
class URPGSkillDefinition;

/**
 * URPGSkillViewModel
 * 
 * 전체 스킬 창의 상태(남은 SP, 전체 스킬 목록 등)를 관리하는 뷰모델입니다.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGSkillViewModel : public URPGViewModelBase
{
	GENERATED_BODY()

public:
	URPGSkillViewModel();

	// PlayerSkillComponent로부터 데이터를 받아 초기화
	void InitializeSkillData(URPGPlayerSkillComponent* InSkillComponent, const TArray<URPGSkillDefinition*>& InAllSkills);

	// ---------------------------------------------------
	// 바인딩 필드
	// ---------------------------------------------------

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Skill")
	int32 RemainingSP = 0;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Skill")
	int32 TotalSP = 0;

	// 화면에 표시될 개별 스킬 슬롯들의 뷰모델 목록
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Skill")
	TArray<URPGSkillSlotViewModel*> SkillSlots;

	// ---------------------------------------------------
	// 명령 (View -> ViewModel -> Model)
	// ---------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void RequestSkillLevelUp(FGameplayTag SkillTag);

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void RequestSkillLevelDown(FGameplayTag SkillTag);

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void RequestSkillLevelMax(FGameplayTag SkillTag);

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void RequestSkillLevelMin(FGameplayTag SkillTag);

	// 데이터 갱신 (Model -> ViewModel)
	void RefreshSkillData();

private:
	UPROPERTY()
	TObjectPtr<URPGPlayerSkillComponent> SkillComponent;
};
