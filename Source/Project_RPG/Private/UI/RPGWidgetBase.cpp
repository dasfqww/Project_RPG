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
	if (bCanDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
	    // 드래그 시작
	    bIsDragging = true;
	    InitialMousePosition = MouseEvent.GetScreenSpacePosition();
	    InitialWidgetPosition = MyGeometry.GetAbsolutePosition();
	
	    return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
	}
	return Super::NativeOnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bIsDragging)
	{
	    // 드래그 중에 위치 업데이트
	    FVector2D MouseDelta = MouseEvent.GetScreenSpacePosition() - InitialMousePosition;
	    FVector2D NewPosition = InitialWidgetPosition + MouseDelta;
	    this->SetRenderTranslation(NewPosition);
	}

	return Super::NativeOnMouseMove(MyGeometry, MouseEvent);
}

FReply URPGWidgetBase::NativeOnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bCanDrag && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
	    // 드래그 끝
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
