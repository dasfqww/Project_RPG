// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGInteractionWidget.generated.h"

class ARPGPlayer;
struct FInteractableData;
class UTextBlock;
class UProgressBar;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInteractionWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category = "Interaction Widget|Player Ref")
	TObjectPtr<ARPGPlayer> PlayerReference;

	void UpdateWidget(const FInteractableData* InteractableData) const;

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

protected:
	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UTextBlock> NameText;
	
	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UTextBlock> ActionText;
	
	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UTextBlock> QuantityText;
	
	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UTextBlock> KeyPressText;
	
	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UProgressBar> InteractionProgressBar;

	UFUNCTION(Category = "Interaction Widget | Interactable Data")
	float UpdateInteractionProgress();
};
