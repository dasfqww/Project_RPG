// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MVVMViewModelBase.h"
#include "Type/RPGStructTypes.h"
#include "GameplayTagContainer.h"
#include "RPGSkillSlotViewModel.generated.h"

class URPGSkillDefinition;
class URPGPlayerSkillComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnNextLevelCostChanged, int32 /*NewCost*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnTripodIndicesChanged, const TArray<int32>& /*NewIndices*/);

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
	URPGSkillDefinition* GetSkillDefinition() const { return SkillDefinition; }

	FOnNextLevelCostChanged OnNextLevelCostChanged;
	FOnTripodIndicesChanged OnTripodIndicesChanged;

	// UI에서 바인딩할 필드들
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	UTexture2D* SkillIcon;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	FText SkillName;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	int32 SkillLevel = 1;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	int32 NextLevelCost = 0;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	bool bIsLocked = false;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Settings")
	TArray<int32> CurrentTripodIndices;

	void SetOwnerComponent(URPGPlayerSkillComponent* InComp);

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill")
	void RequestTripodSelection(int32 Tier, int32 OptionIndex);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill")
	bool IsTripodSelected(int32 Tier, int32 OptionIndex) const;

private:
	UPROPERTY()
	TObjectPtr<URPGSkillDefinition> SkillDefinition;

	TWeakObjectPtr<URPGPlayerSkillComponent> OwnerSkillComponent;
};
