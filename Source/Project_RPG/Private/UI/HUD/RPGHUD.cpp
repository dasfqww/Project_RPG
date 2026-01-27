// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/HUD/RPGHUD.h"
#include "UI/RPGMainMenuWidget.h"
#include "UI/RPGInteractionWidget.h"

ARPGHUD::ARPGHUD()
{
}

void ARPGHUD::BeginPlay()
{
	Super::BeginPlay();

	if (MainMenuWidgetClass)
	{
		//드래그드롭이 안된다면 AddToViewport(zorder)<<조정할것
		MainMenuWidget = CreateWidget<URPGMainMenuWidget>(GetWorld(), MainMenuWidgetClass);
		MainMenuWidget->AddToViewport(-1);
		MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (InteractionWidgetClass)
	{
		InteractionWidget = CreateWidget<URPGInteractionWidget>(GetWorld(), InteractionWidgetClass);
		InteractionWidget->AddToViewport(-1);
		InteractionWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ARPGHUD::DisplayMenu()
{
	if (MainMenuWidget)
	{
		bIsMenuVisible = true;
		MainMenuWidget->SetVisibility(ESlateVisibility::Visible);
	}
}

void ARPGHUD::HideMenu()
{
	if (MainMenuWidget)
	{
		bIsMenuVisible = false;
		MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ARPGHUD::ToggleMenu()
{
	if (bIsMenuVisible)
	{
		HideMenu();

		const FInputModeGameOnly InputMode;
		GetOwningPlayerController()->SetInputMode(InputMode);
		GetOwningPlayerController()->SetShowMouseCursor(false);
	}

	else
	{
		DisplayMenu();

		const FInputModeGameAndUI InputMode;
		GetOwningPlayerController()->SetInputMode(InputMode);
		GetOwningPlayerController()->SetShowMouseCursor(true);
	}
}

void ARPGHUD::ShowInteractionWidget()
{
	if (InteractionWidget)
	{
		InteractionWidget->SetVisibility(ESlateVisibility::Visible);
	}
}

void ARPGHUD::HideInteractionWidget()
{
	if (InteractionWidget)
	{
		InteractionWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

//void ARPGHUD::UpdateInteractionWidget(const FInteractableData* InteractableData)
//{
//	if (InteractionWidget)
//	{
//		if (InteractionWidget->GetVisibility()==ESlateVisibility::Collapsed)
//		{
//			InteractionWidget->SetVisibility(ESlateVisibility::Visible);
//		}
//
//		InteractionWidget->UpdateWidget(InteractableData);
//	}
//}
