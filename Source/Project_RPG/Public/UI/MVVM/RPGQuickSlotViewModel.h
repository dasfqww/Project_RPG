// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "RPGQuickSlotViewModel.generated.h"

class URPGItemBase;
class UQuickSlotComponent;
class UTexture2D; // 전방 선언 추가

/**
 * 퀵슬롯 개별 칸의 데이터를 관리하고 UI에 전달하는 뷰모델입니다.
 */
UCLASS(BlueprintType) // BlueprintType 추가
class PROJECT_RPG_API URPGQuickSlotViewModel : public URPGViewModelBase
{
	GENERATED_BODY()

public:
	/** 뷰모델 초기화 */
	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void Initialize(int32 InSlotIndex, UQuickSlotComponent* InComponent);

private:
	/** 컴포넌트의 슬롯 변경 델리게이트를 처리할 함수 */
	UFUNCTION()
	void HandleSlotChanged(int32 SlotIndex, URPGItemBase* NewItem);

	/** 아이템 수량 변경 델리게이트를 처리할 함수 */
	UFUNCTION()
	void HandleQuantityChanged(URPGItemBase* Item, int32 NewQuantity);

	/** 아이템 데이터를 UI용 정보로 변환 */
	void UpdateFromItem(URPGItemBase* InItem);

private:
	// --- UI 바인딩용 프로퍼티 (FieldNotify) ---

	/** 아이템 아이콘 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetItemIcon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTexture2D> ItemIcon;

	/** 아이템 개수 텍스트 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetQuantityText", meta = (AllowPrivateAccess = "true"))
	FText QuantityText;

	/** 슬롯 활성화 여부 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetIsSlotActive", meta = (AllowPrivateAccess = "true"))
	bool bIsSlotActive;

	/** 입력 키 텍스트 */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Setter = "SetInputKeyText", meta = (AllowPrivateAccess = "true"))
	FText InputKeyText;

public:
	// Setter 함수들은 반드시 UFUNCTION이어야 하며, 매크로에서 지정한 이름과 일치해야 합니다.
	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetItemIcon(UTexture2D* InIcon);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetQuantityText(FText InText);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetIsSlotActive(bool bInActive);

	UFUNCTION(BlueprintCallable, Category = "RPG|ViewModel")
	void SetInputKeyText(FText InText);

private:
	/** 현재 이 뷰모델이 담당하는 슬롯 번호 */
	int32 TargetSlotIndex;

	/** 연동된 퀵슬롯 컴포넌트 (약참조) */
	UPROPERTY()
	TWeakObjectPtr<UQuickSlotComponent> LinkedComponent;
};
