// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGSkillWindow.generated.h"

class URPGSkillViewModel;
class URPGSkillListModule;
class URPGSkillDetailModule;

/**
 * URPGSkillWindow
 * 
 * 스킬 시스템의 메인 UI 창입니다.
 * ViewModel을 생성하고 하위 모듈들에게 데이터를 공급합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillWindow : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	// 외부(PlayerUIComponent 등)에서 창을 열 때 호출
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitializeSkillWindow();

	// 스킬 리스트에서 항목이 선택되었을 때 호출 (Blueprint에서 호출하거나 델리게이트 바인딩)
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void HandleSkillSelection(URPGSkillSlotViewModel* SelectedSlotVM);

protected:
	virtual void NativeConstruct() override;

	// ViewModel 인스턴스
	UPROPERTY(BlueprintReadOnly, Category = "RPG|MVVM")
	TObjectPtr<URPGSkillViewModel> SkillViewModel;

	// 하위 위젯 (BindWidget으로 UMG와 연결)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGSkillListModule> SkillListModule;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<URPGSkillDetailModule> SkillDetailModule;
};
