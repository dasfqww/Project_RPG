// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGWidgetBase.h"
#include "Interface/PawnUIInterface.h"

void URPGWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(GetOwningPlayerPawn()))
	{
		if (UPlayerUIComponent* PlayerUIComponent = PawnUIInterface->GetPlayerUIComponent())
		{
			BP_OnOwningPlayerUIComponentInitialized(PlayerUIComponent);
		}
	}
}

FReply URPGWidgetBase::NativeOnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	bool bShouldStartDrag = false;

	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
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

		return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
	}

	return Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
		FVector2D MouseDelta = MouseEvent.GetScreenSpacePosition() - InitialMousePosition;
		FVector2D NewTranslation = InitialRenderTranslation + MouseDelta;
		
		SetRenderTranslation(NewTranslation);
	}

	return Super::NativeOnMouseMove(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(MyGeometry, MouseEvent);
}

void URPGWidgetBase::InitNPCCreatedWidget(AActor* OwningEnemyActor)
{
	if (IPawnUIInterface* PawnUIInterface = Cast<IPawnUIInterface>(OwningEnemyActor))
	{
		UNPCUIComponent* NPCUIComponent = PawnUIInterface->GetNPCUIComponent();

		checkf(NPCUIComponent, TEXT("Failed to extrac an NPCUIComponent from %s"), 
			*OwningEnemyActor->GetActorNameOrLabel());

		BP_OnOwningNPCUIComponentInitialized(NPCUIComponent);
	}
}