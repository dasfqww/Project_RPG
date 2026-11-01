// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Layout/SlateRect.h"
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
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Owning Player UI Component Initialized"))
		void BP_OnOwningPlayerUIComponentInitialized(UPlayerUIComponent* OwningPlayerUIComponent);
	
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "On Owning NPC UI Component Initialized"))
		void BP_OnOwningNPCUIComponentInitialized(UNPCUIComponent* OwningNPCUIComponent);

	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Existing windows keep left-button dragging unless they opt into another button. */
	virtual FKey GetWindowDragMouseButton() const;

	/** Opt-in screen-space boundary; other existing windows remain unconstrained. */
	virtual TOptional<FSlateRect> GetWindowDragScreenBounds() const;

	UPROPERTY(EditDefaultsOnly, Category = "Drag")
	bool bCanDrag = false;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Drag")
	class UWidget* TitleBar;

private:
	void StopWindowDrag();

	FVector2D InitialMousePosition;
	FVector2D InitialRenderTranslation;
	FSlateRect InitialTitleBounds;
	float InitialGeometryScale = 1.f;
	bool bIsDragging = false;
	bool bRemovingFromParent = false;

public:
	/** Raise/focus a player window without consuming its child controls' clicks. */
	FSimpleMulticastDelegate OnWindowInteraction;

	/** Release popup registration as soon as removal is requested. */
	virtual void RemoveFromParent() override;
	bool IsRemovingFromParent() const { return bRemovingFromParent; }

	UFUNCTION(BlueprintCallable)
		void InitNPCCreatedWidget(AActor* OwningEnemyActor);
};
