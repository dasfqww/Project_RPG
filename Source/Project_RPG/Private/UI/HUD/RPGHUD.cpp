// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/HUD/RPGHUD.h"
#include "UI/RPGMainMenuWidget.h"
#include "UI/RPGInteractionWidget.h"
#include "UI/Option/RPGOptionMenu.h"

ARPGHUD::ARPGHUD()
{
}

void ARPGHUD::BeginPlay()
{
	Super::BeginPlay();

	if (MainMenuWidgetClass)
	{
		//�巡�׵���� �ȵȴٸ� AddToViewport(zorder)<<�����Ұ�
		MainMenuWidget = CreateWidget<URPGMainMenuWidget>(GetWorld(), MainMenuWidgetClass);
		MainMenuWidget->AddToViewport(-1);
		MainMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (OptionMenuWidgetClass)
	{
		OptionMenuWidget = CreateWidget<URPGOptionMenu>(GetWorld(), OptionMenuWidgetClass);
		OptionMenuWidget->AddToViewport(10);
		OptionMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
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
	if (bIsOptionMenuVisible)
	{
		HideOptionMenu();
	}

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

void ARPGHUD::DisplayOptionMenu()
{
	if (OptionMenuWidget)
	{
		bIsOptionMenuVisible = true;
		OptionMenuWidget->SetVisibility(ESlateVisibility::Visible);
		OptionMenuWidget->OnMenuShown();
	}
}

void ARPGHUD::HideOptionMenu()
{
	if (OptionMenuWidget)
	{
		bIsOptionMenuVisible = false;
		OptionMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void ARPGHUD::ToggleOptionMenu()
{
	if (bIsOptionMenuVisible)
	{
		HideOptionMenu();

		const FInputModeGameOnly InputMode;
		GetOwningPlayerController()->SetInputMode(InputMode);
		GetOwningPlayerController()->SetShowMouseCursor(false);
	}
	else
	{
		DisplayOptionMenu();

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
