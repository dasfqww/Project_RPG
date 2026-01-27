// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "RPGWidgetBase.generated.h"

class UPlayerUIComponent;
class UNPCUIComponent;
class UDragItemVisual;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGWidgetBase : public UCommonActivatableWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Owning Player UI Component Initialized"))
		void BP_OnOwningPlayerUIComponentInitialized(UPlayerUIComponent* OwningPlayerUIComponent);
	
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Owning NPC UI Component Initialized"))
		void BP_OnOwningNPCUIComponentInitialized(UNPCUIComponent* OwningNPCUIComponent);

	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

	UPROPERTY(EditDefaultsOnly, Category = "Drag")
	bool bCanDrag = false;

private:
	FVector2D InitialMousePosition;
	FVector2D InitialWidgetPosition;
	bool bIsDragging = false;

public:
	UFUNCTION(BlueprintCallable)
		void InitNPCCreatedWidget(AActor* OwningEnemyActor);
};
