// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/HUD/RPGHUD.h"
#include "UI/RPGMainMenuWidget.h"
#include "UI/RPGInteractionWidget.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UI/Class/RPGClassSelectionWidget.h"
#include "Player/RPGPlayerState.h"

ARPGHUD::ARPGHUD()
{
	ClassSelectionWidgetClass = TSoftClassPtr<URPGClassSelectionWidget>(FSoftObjectPath(
		TEXT("/GladiatorCore/UI_New/Widgets/Class/W_Class_ClassSelection.W_Class_ClassSelection_C")));
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

	TryDisplayInitialClassSelection();
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

bool ARPGHUD::CreateClassSelectionWidget()
{
	if (ClassSelectionWidget)
	{
		return true;
	}

	APlayerController* PlayerController = GetOwningPlayerController();
	const TSubclassOf<URPGClassSelectionWidget> WidgetClass = ClassSelectionWidgetClass.LoadSynchronous();
	if (!PlayerController || !WidgetClass)
	{
		return false;
	}

	ClassSelectionWidget = CreateWidget<URPGClassSelectionWidget>(PlayerController, WidgetClass);
	if (!ClassSelectionWidget)
	{
		return false;
	}

	ClassSelectionWidget->AddToViewport(100);
	ClassSelectionWidget->SetVisibility(ESlateVisibility::Collapsed);
	ClassSelectionWidget->OnDeactivated().AddUObject(this, &ThisClass::HandleClassSelectionDeactivated);
	return true;
}

void ARPGHUD::DisplayClassSelection()
{
	if (!CreateClassSelectionWidget())
	{
		return;
	}

	ClassSelectionWidget->SetVisibility(ESlateVisibility::Visible);
	ClassSelectionWidget->ActivateWidget();

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(ClassSelectionWidget->TakeWidget());
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(true);
	}
}

void ARPGHUD::HideClassSelection()
{
	if (!ClassSelectionWidget)
	{
		return;
	}

	if (ClassSelectionWidget->IsActivated())
	{
		ClassSelectionWidget->DeactivateWidget();
		return;
	}

	HandleClassSelectionDeactivated();
}

void ARPGHUD::ToggleClassSelection()
{
	if (ClassSelectionWidget && ClassSelectionWidget->IsActivated())
	{
		HideClassSelection();
	}
	else
	{
		DisplayClassSelection();
	}
}

void ARPGHUD::HandleClassSelectionDeactivated()
{
	if (ClassSelectionWidget)
	{
		ClassSelectionWidget->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (APlayerController* PlayerController = GetOwningPlayerController())
	{
		const FInputModeGameOnly InputMode;
		PlayerController->SetInputMode(InputMode);
		PlayerController->SetShowMouseCursor(false);
	}
}

void ARPGHUD::TryDisplayInitialClassSelection()
{
	APlayerController* PlayerController = GetOwningPlayerController();
	ARPGPlayerState* PlayerState = PlayerController
		? PlayerController->GetPlayerState<ARPGPlayerState>()
		: nullptr;
	if (!PlayerState)
	{
		GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::TryDisplayInitialClassSelection);
		return;
	}

	if (PlayerState->GetSelectedClass() == ERPGGladiatorCharacterClass::Count)
	{
		DisplayClassSelection();
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
