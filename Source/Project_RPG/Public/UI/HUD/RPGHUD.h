// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include <Interface/InteractionInterface.h>
#include "RPGHUD.generated.h"

class URPGInteractionWidget;
class URPGMainMenuWidget;

/**
 * 코드 쳐보고 필요시 리팩토링 고려할것.
 */
UCLASS()
class PROJECT_RPG_API ARPGHUD : public AHUD
{
	GENERATED_BODY()
public:
	ARPGHUD();

	virtual void BeginPlay() override;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGMainMenuWidget> MainMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;
	
	/*UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;*/

	bool bIsMenuVisible;

	void DisplayMenu();
	void HideMenu();
	void ToggleMenu();

	void ShowInteractionWidget();
	void HideInteractionWidget();
	//void UpdateInteractionWidget(const FInteractableData* InteractableData);

protected:
	UPROPERTY()
	URPGMainMenuWidget* MainMenuWidget;

	UPROPERTY()
	URPGInteractionWidget* InteractionWidget;
};
