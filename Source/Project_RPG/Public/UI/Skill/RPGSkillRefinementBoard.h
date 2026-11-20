// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGSkillRefinementBoard.generated.h"

class URPGSkillSlotViewModel;
class URPGSkillRefinementButton;

/**
 * 스킬 연마 선택 보드. 기존 3열 보드와 첫 단계 4열 보드를 지원한다.
 */
UCLASS(meta = (DisplayName = "스킬 연마 보드"))
class PROJECT_RPG_API URPGSkillRefinementBoard : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitializeBoard(URPGSkillSlotViewModel* InSlotVM);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;

	// 티어 1
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_1_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_1_2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_1_3;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<URPGSkillRefinementButton> Btn_1_4;

	// 티어 2 (3개 선택지)
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_2_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_2_2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_2_3;

	// 티어 3 (2개 선택지)
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_3_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGSkillRefinementButton> Btn_3_2;

private:
	TWeakObjectPtr<URPGSkillSlotViewModel> CurrentSlotVM;

	// 내부 관리를 위한 배열 (티어별로 그룹화)
	TArray<TArray<URPGSkillRefinementButton*>> RefinementTiers;

	void UpdateButtons();
	void RefreshSelection();
	void OnRefinementBtnClicked(int32 Tier, int32 Index);
	void UpdateRefinementSelection(const TArray<int32>& NewIndices);
};
