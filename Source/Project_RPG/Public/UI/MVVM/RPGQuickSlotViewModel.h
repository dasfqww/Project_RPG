// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "Type/RPGStructTypes.h" // FRPGQuickSlotContent 사용을 위해 인클루드
#include "RPGQuickSlotViewModel.generated.h"

class URPGItemBase;
class UQuickSlotComponent;
class UTexture2D;

/**
 * 퀵슬롯 개별 칸의 데이터를 관리하고 UI에 전달하는 뷰모델입니다.
 * 전투 스킬(Q,E,R,F,1-4)과 아이템(F1-F8) 모두에서 사용될 수 있습니다.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGQuickSlotViewModel : public URPGViewModelBase
{
	GENERATED_BODY()

public:
	/** 뷰모델 초기화 (bIsSkillSlot으로 타입 구분) */
	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void Initialize(int32 InSlotIndex, UQuickSlotComponent* InComponent, bool bIsSkillSlot);

private:
	/** 슬롯 내용물 변경 처리 */
	UFUNCTION()
	void HandleSlotChanged(int32 SlotIndex, const FRPGQuickSlotContent& NewContent);

	/** 아이템 수량 변경 처리 (아이템 슬롯인 경우에만 반응) */
	UFUNCTION()
	void HandleQuantityChanged(URPGItemBase* Item, int32 NewQuantity);

	/** 데이터를 UI용 정보로 변환 (통합 처리) */
	void UpdateFromContent(const FRPGQuickSlotContent& InContent);

private:
	// --- UI 바인딩용 프로퍼티 (FieldNotify) ---

	/** 슬롯 아이콘 (아이템 또는 스킬 아이콘) */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetItemIcon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ItemIcon;

	/** 아이템 개수 텍스트 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetQuantityText", meta = (AllowPrivateAccess = "true"))
	FText QuantityText;

	/** 슬롯 활성화 여부 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetIsSlotActive", meta = (AllowPrivateAccess = "true"))
	bool bIsSlotActive;

	/** 입력 키 텍스트 (예: Q, E, F1 등) */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetInputKeyText", meta = (AllowPrivateAccess = "true"))
	FText InputKeyText;

public:
	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetItemIcon(UTexture2D* InIcon);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetQuantityText(FText InText);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetIsSlotActive(bool bInActive);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetInputKeyText(FText InText);

private:
	int32 TargetSlotIndex;
	bool bIsSkillSlotViewModel;

	UPROPERTY()
	TWeakObjectPtr<UQuickSlotComponent> LinkedComponent;
};