// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Type/RPGStructTypes.h"
#include "GameplayTagContainer.h"
#include "RPGSkillSlotViewModel.generated.h"

class URPGSkillDefinition;

/**
 * URPGSkillSlotViewModel
 * 
 * 개별 스킬 슬롯 위젯에 바인딩될 데이터 모델입니다.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGSkillSlotViewModel : public UMVVMViewModelBase
{
	GENERATED_BODY()

public:
	void SetSkillDefinition(URPGSkillDefinition* InDefinition);
	void RefreshFromSaveData(const FRPGSkillSaveData& Data);

	FGameplayTag GetSkillTag() const;

	// UI에서 바인딩할 필드들
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	UTexture2D* SkillIcon;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	FText SkillName;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	int32 SkillLevel = 1;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	bool bIsLocked = false;

private:
	UPROPERTY()
	TObjectPtr<URPGSkillDefinition> SkillDefinition;
};
