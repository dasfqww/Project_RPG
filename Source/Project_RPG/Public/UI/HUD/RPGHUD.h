// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include <Interface/InteractionInterface.h>
#include "RPGHUD.generated.h"

class URPGInteractionWidget;
class URPGMainMenuWidget;
class URPGClassSelectionWidget;

/**
 * �ڵ� �ĺ��� �ʿ�� �����丵 �����Ұ�.
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
	TSubclassOf<class URPGOptionMenu> OptionMenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;

	/** Class selection supplied by the GladiatorCore game feature. */
	UPROPERTY(EditDefaultsOnly, Category = "Widgets|Class Selection")
	TSoftClassPtr<URPGClassSelectionWidget> ClassSelectionWidgetClass;
	
	/*UPROPERTY(EditDefaultsOnly, Category = "Widgets")
	TSubclassOf<URPGInteractionWidget> InteractionWidgetClass;*/

	bool bIsMenuVisible;
	bool bIsOptionMenuVisible;

	void DisplayMenu();
	void HideMenu();
	void ToggleMenu();

	void DisplayOptionMenu();
	void HideOptionMenu();
	void ToggleOptionMenu();

	void ShowInteractionWidget();
	void HideInteractionWidget();

	UFUNCTION(BlueprintCallable, Category = "Widgets|Class Selection")
	void DisplayClassSelection();

	UFUNCTION(BlueprintCallable, Category = "Widgets|Class Selection")
	void HideClassSelection();

	UFUNCTION(BlueprintCallable, Category = "Widgets|Class Selection")
	void ToggleClassSelection();
	//void UpdateInteractionWidget(const FInteractableData* InteractableData);

protected:
	UPROPERTY()
	URPGMainMenuWidget* MainMenuWidget;

	UPROPERTY()
	class URPGOptionMenu* OptionMenuWidget;

	UPROPERTY()
	URPGInteractionWidget* InteractionWidget;

	UPROPERTY(Transient)
	TObjectPtr<URPGClassSelectionWidget> ClassSelectionWidget;

private:
	bool CreateClassSelectionWidget();
	void HandleClassSelectionDeactivated();
	void TryDisplayInitialClassSelection();
};
