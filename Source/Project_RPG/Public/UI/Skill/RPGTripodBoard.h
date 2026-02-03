// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "INotifyFieldValueChanged.h"
#include "RPGTripodBoard.generated.h"

class UVerticalBox;
class URPGSkillSlotViewModel;

/**
 * 트라이포드 선택 보드
 * - 스킬 정의에 따라 동적으로 트라이포드 버튼을 생성
 * - 선택 상태 관리 및 ViewModel 연동
 */
UCLASS()
class PROJECT_RPG_API URPGTripodBoard : public URPGWidgetBase
{
	GENERATED_BODY()
	
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitializeBoard(URPGSkillSlotViewModel* InSlotVM);

protected:
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UVerticalBox> TripodContainer;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class URPGTripodButton> TripodButtonClass;

private:
	TWeakObjectPtr<URPGSkillSlotViewModel> CurrentSlotVM;
	
	// 생성된 버튼들 관리 (Tier -> Button Array)
	TMap<int32, TArray<class URPGTripodButton*>> CreatedButtons;

	void CreateButtons();
	void RefreshSelection();
	void OnTripodBtnClicked(int32 Tier, int32 Index);
	
	// 커스텀 델리게이트용 콜백 (에러가 날 수 없는 단순 구조)
	void UpdateTripodSelection(const TArray<int32>& NewIndices);
};