// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Type/RPGStructTypes.h"
#include "RPGDragDropOperation.generated.h"

/**
 * 아이템과 스킬을 모두 아우르는 통합 드래그 앤 드롭 오퍼레이션 클래스입니다.
 */
UCLASS()
class PROJECT_RPG_API URPGDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()
public:
	/** 드래그 중인 내용물 (아이템 또는 스킬 태그) */
	UPROPERTY(BlueprintReadWrite, Category = "RPG|DragDrop")
	FRPGQuickSlotContent DraggedContent;

	/** 드래그가 시작된 슬롯 인덱스 (인벤토리나 퀵슬롯에서의 위치) */
	UPROPERTY(BlueprintReadWrite, Category = "RPG|DragDrop")
	int32 SourceSlotIndex = INDEX_NONE;

	/** 드래그가 시작된 컴포넌트 참조 (QuickSlotComponent 또는 InventoryComponent) */
	UPROPERTY(BlueprintReadWrite, Category = "RPG|DragDrop")
	TObjectPtr<UActorComponent> SourceComponent;

	/** 퀵슬롯에서 시작된 드래그인지 여부 (스왑/이동 로직용) */
	UPROPERTY(BlueprintReadWrite, Category = "RPG|DragDrop")
	bool bIsFromQuickSlot = false;
};
