// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/NPCUIComponent.h"
#include "UI/RPGWidgetBase.h"

void UNPCUIComponent::RegisterNPCDrawnWidget(URPGWidgetBase* InWidgetToRegister)
{
	EnemyDrawnWidgets.Add(InWidgetToRegister);
}

void UNPCUIComponent::RemoveNPCDrawnWidgetsIfAny()
{
	if (EnemyDrawnWidgets.IsEmpty())
	{
		return;
	}

	for (URPGWidgetBase* DrawnWidget : EnemyDrawnWidgets)
	{
		if (DrawnWidget)
		{
			DrawnWidget->RemoveFromParent();
		}
	}

	EnemyDrawnWidgets.Empty();
}
