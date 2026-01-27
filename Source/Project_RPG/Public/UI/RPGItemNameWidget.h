// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGItemNameWidget.generated.h"

//class ARPGPlayer;
class UTextBlock;
//struct FInteractableData;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGItemNameWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	/*UPROPERTY(VisibleAnywhere, Category = "Interaction Widget|Player Ref")
	TObjectPtr<ARPGPlayer> PlayerReference;*/

	UPROPERTY(VisibleAnywhere, meta = (BindWidget), Category = "Interaction Widget| Interactable Data")
	TObjectPtr<UTextBlock> ItemNameText;
	

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	//void InitItemText(const FInteractableData* InteractableData) const;

	
};
