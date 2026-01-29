// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGSkillDetailModule.generated.h"

class URPGSkillDefinition;
class URPGSkillSlotViewModel;

/**
 * URPGSkillDetailModule
 * 
 * 선택된 스킬의 상세 정보(설명, 트라이포드 설정 등)를 표시하는 위젯입니다.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillDetailModule : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	// 선택된 스킬이 변경되었을 때 호출
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void SetSelectedSkill(URPGSkillSlotViewModel* InSkillSlotVM);

protected:
	// UI 바인딩용 이벤트 (Blueprint에서 Text, Image 설정)
	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI")
	void OnSkillSelected(URPGSkillSlotViewModel* SkillSlotVM, const URPGSkillDefinition* SkillDef);

	UFUNCTION(BlueprintImplementableEvent, Category = "RPG|UI")
	void OnNoSkillSelected();

private:
	UPROPERTY()
	TObjectPtr<URPGSkillSlotViewModel> CurrentSkillVM;
};
