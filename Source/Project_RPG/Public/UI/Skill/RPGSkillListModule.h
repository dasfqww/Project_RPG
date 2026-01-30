// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGSkillListModule.generated.h"

class UCommonListView;
class URPGSkillSlotViewModel;

// 스킬 선택 시 알림을 보낼 델리게이트 선언
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSkillSelectedDelegate, URPGSkillSlotViewModel*, SelectedSlotVM);

/**
 * URPGSkillListModule
 * 
 * 스킬 목록(ListView)을 관리하는 모듈입니다.
 * 데이터 바인딩과 리스트 아이템 선택 이벤트를 처리합니다.
 */
UCLASS()
class PROJECT_RPG_API URPGSkillListModule : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	// 초기화: 리스트에 데이터 설정
	UFUNCTION(BlueprintCallable, Category = "RPG|UI")
	void InitSkillList(const TArray<URPGSkillSlotViewModel*>& SkillSlots);

	// 스킬 선택 이벤트 (Window 등에서 바인딩)
	UPROPERTY(BlueprintAssignable, Category = "RPG|Event")
	FOnSkillSelectedDelegate OnSkillSelected;

protected:
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleItemSelectionChanged(UObject* Item);

protected:
	// UMG의 ListView 위젯과 바인딩 (이름을 일치시켜야 함)
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonListView> SkillListView;
};
