// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGInteractionWidget.h"
#include "Components/ProgressBar.h"
#include "Interface/InteractionInterface.h"
#include "Components/TextBlock.h"

void URPGInteractionWidget::UpdateWidget(const FInteractableData* InteractableData) const
{
	switch (InteractableData->InteractableType)
	{
	case EInteractableType::PickUp:
		KeyPressText->SetText(FText::FromString("Press"));
		InteractionProgressBar->SetVisibility(ESlateVisibility::Collapsed);

		if (InteractableData->Quantity<2)
		{
			QuantityText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			QuantityText->SetText(FText::Format(NSLOCTEXT("InteractionWidget", "QuantityText", "x{0}"),
				InteractableData->Quantity));
		}

		break;

	case EInteractableType::NPC:
		break;
	
	case EInteractableType::Device:

		break;

	}

	ActionText->SetText(InteractableData->Action);
	NameText->SetText(InteractableData->Name);
}

void URPGInteractionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	InteractionProgressBar->PercentDelegate.BindUFunction(this, "UpdateInteractionProgress");
}

void URPGInteractionWidget::NativeConstruct()
{
	Super::NativeConstruct();


}

float URPGInteractionWidget::UpdateInteractionProgress()
{
	return 0.0f;
}
