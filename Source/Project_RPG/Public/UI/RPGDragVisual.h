// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "Components/Image.h"
#include "RPGDragVisual.generated.h"

/**
 * 아이템이나 스킬을 드래그할 때 마우스를 따라다니는 시각적 위젯의 베이스 클래스입니다.
 */
UCLASS()
class PROJECT_RPG_API URPGDragVisual : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	/** 마우스를 따라다닐 아이콘 이미지 */
	UPROPERTY(VisibleAnywhere, Category = "RPG|DragVisual", meta = (BindWidget))
	TObjectPtr<UImage> IconImage;

	/** 외부에서 아이콘 텍스처를 설정하기 위한 함수 */
	UFUNCTION(BlueprintCallable, Category = "RPG|DragVisual")
	void SetIcon(UTexture2D* InTexture)
	{
		if (IconImage && InTexture)
		{
			IconImage->SetBrushFromTexture(InTexture);
		}
	}
};
