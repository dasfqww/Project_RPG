// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGMainMenuWidget.generated.h"

class ARPGPlayer;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGMainMenuWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:

	UPROPERTY()
	TObjectPtr<ARPGPlayer> PlayerCharacter;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, 
		const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
};
