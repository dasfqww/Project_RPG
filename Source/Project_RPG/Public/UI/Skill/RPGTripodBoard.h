// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGTripodBoard.generated.h"

class URPGSkillSlotViewModel;
class URPGTripodButton;

/**
 * 트라이포드 선택 보드 (고정형 3-3-2 구조)
 * - WBP에서 8개의 버튼을 미리 배치하고 BindWidget으로 연결
 */
UCLASS()
class PROJECT_RPG_API URPGTripodBoard : public URPGWidgetBase
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitializeBoard(URPGSkillSlotViewModel* InSlotVM);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativePreConstruct() override;
	virtual void NativeDestruct() override;

	// 티어 1 (3개 선택지)
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_1_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_1_2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_1_3;

	// 티어 2 (3개 선택지)
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_2_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_2_2;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_2_3;

	// 티어 3 (2개 선택지)
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_3_1;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<URPGTripodButton> Btn_3_2;

private:
	TWeakObjectPtr<URPGSkillSlotViewModel> CurrentSlotVM;
	
	// 내부 관리를 위한 배열 (티어별로 그룹화)
	TArray<TArray<URPGTripodButton*>> TripodTiers;

	void UpdateButtons();
	void RefreshSelection();
	void OnTripodBtnClicked(int32 Tier, int32 Index);
	void UpdateTripodSelection(const TArray<int32>& NewIndices);
};
