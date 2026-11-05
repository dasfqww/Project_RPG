// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGWidgetBase.h"
#include "Manager/UIManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/RPGWindowDragConstraints.h"
#include "Framework/Application/SlateApplication.h"
#include "Interface/PawnUIInterface.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Component/UI/NPCUIComponent.h"

void URPGWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(GetOwningPlayerPawn()))
	{
		if (UPlayerUIComponent* PlayerUIComponent = Cast<UPlayerUIComponent>(PawnUIInterface->GetPawnUIComponent()))
		{
			BP_OnOwningPlayerUIComponentInitialized(PlayerUIComponent);
		}
	}
}

FKey URPGWidgetBase::GetWindowDragMouseButton() const
{
	return EKeys::LeftMouseButton;
}

TOptional<FSlateRect> URPGWidgetBase::GetWindowDragScreenBounds() const
{
	return {};
}

void URPGWidgetBase::StopWindowDrag()
{
	bIsDragging = false;
	if (FSlateApplication::IsInitialized() && HasMouseCapture())
	{
		FSlateApplication::Get().ReleaseMouseCapture();
	}
}

void URPGWidgetBase::NativeOnDeactivated()
{
	StopWindowDrag();
	Super::NativeOnDeactivated();
}

void URPGWidgetBase::RemoveFromParent()
{
	if (bRemovingFromParent) { return; }
	// BeginDestroy calls this virtual function. Keep GC on the engine's existing path.
	if (HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || IsUnreachable())
	{
		Super::RemoveFromParent();
		return;
	}

	// Focus and destruction callbacks may collect garbage or request removal again.
	TStrongObjectPtr<URPGWidgetBase> KeepAlive(this);
	TGuardValue<bool> RemovingGuard(bRemovingFromParent, true);
	StopWindowDrag();
	UUIManager::Get(this)->ForgetUI(this);
	Super::RemoveFromParent();
}

void URPGWidgetBase::NativeDestruct()
{
	UUIManager::Get(this)->ForgetUI(this);
	StopWindowDrag();
	Super::NativeDestruct();
}

FReply URPGWidgetBase::NativeOnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	OnWindowInteraction.Broadcast();
	return Super::NativeOnPreviewMouseButtonDown(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bool bShouldStartDrag = false;

	if (!bIsDragging && MouseEvent.GetEffectingButton() == GetWindowDragMouseButton())
	{
		if (TitleBar)
		{
			if (TitleBar->GetCachedGeometry().IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
			{
				bShouldStartDrag = true;
			}
		}
		else if (bCanDrag)
		{
			bShouldStartDrag = true;
		}
	}

	if (bShouldStartDrag)
	{
		bIsDragging = true;
		InitialMousePosition = MouseEvent.GetScreenSpacePosition();
		InitialRenderTranslation = GetRenderTransform().Translation;
		// Freeze this at press time: cached geometry can lag behind multiple moves
		// in one frame, so clamping against the current translation would drift.
		InitialTitleBounds = TitleBar
			? TitleBar->GetCachedGeometry().GetRenderBoundingRect()
			: MyGeometry.GetRenderBoundingRect();
		// Pointer positions are screen pixels; render translation uses layout units.
		InitialGeometryScale = FMath::Max(
			MyGeometry.GetAccumulatedLayoutTransform().GetScale(), UE_SMALL_NUMBER);

		return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
	}

	return Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		if (!MouseEvent.IsMouseButtonDown(GetWindowDragMouseButton()))
		{
			bIsDragging = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		FVector2D ScreenDelta = MouseEvent.GetScreenSpacePosition() - InitialMousePosition;
		const TOptional<FSlateRect> ScreenBounds = GetWindowDragScreenBounds();
		if (ScreenBounds.IsSet())
		{
			ScreenDelta = RPGWindowDragConstraints::ClampScreenDelta(
				InitialTitleBounds, ScreenBounds.GetValue(), ScreenDelta);
		}
		const FVector2D NewTranslation = InitialRenderTranslation + ScreenDelta / InitialGeometryScale;
		
		SetRenderTranslation(NewTranslation);
		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging && MouseEvent.GetEffectingButton() == GetWindowDragMouseButton())
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(MyGeometry, MouseEvent);
}

void URPGWidgetBase::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bIsDragging = false;
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

void URPGWidgetBase::InitNPCCreatedWidget(AActor* OwningEnemyActor)
{
	if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(OwningEnemyActor))
	{
		UNPCUIComponent* NPCUIComponent = Cast<UNPCUIComponent>(PawnUIInterface->GetPawnUIComponent());

		checkf(NPCUIComponent, TEXT("Failed to extrac an NPCUIComponent from %s"), 
			*OwningEnemyActor->GetActorNameOrLabel());

		BP_OnOwningNPCUIComponentInitialized(NPCUIComponent);
	}
}
