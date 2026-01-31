// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGSkillLevelAdjuster.generated.h"

class URPGSkillViewModel;
class URPGSkillSlotViewModel;
class URPGCustomButton;
class UCommonTextBlock;

/**
 * URPGSkillLevelAdjuster
 * 
 * 스킬 레벨을 올리거나 내리는 UI 컴포넌트입니다. (+ / - 버튼)
 */
UCLASS()
class PROJECT_RPG_API URPGSkillLevelAdjuster : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	// 초기화: 어떤 스킬을 조작할지와 메인 뷰모델(명령 전달용) 설정
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitializeAdjuster(URPGSkillViewModel* InMainVM, URPGSkillSlotViewModel* InSlotVM);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Context")
	TObjectPtr<URPGSkillViewModel> MainViewModel;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Context")
	TObjectPtr<URPGSkillSlotViewModel> TargetSlotViewModel;

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonTextBlock> RequireLevelText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGCustomButton> Btn_IncreaseLevel;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGCustomButton> Btn_DecreaseLevel;

	UFUNCTION()
	void OnIncreaseLevelClicked();

	UFUNCTION()
	void OnDecreaseLevelClicked();

	// 데이터 변경 감지 콜백 (C++ Native Delegate)
	void OnCostChanged(int32 NewCost);

	void UpdateRequireLevelText();
};
