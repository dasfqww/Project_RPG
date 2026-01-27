// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Component/UI/PawnUIComponent.h"
#include "NPCUIComponent.generated.h"

class URPGWidgetBase;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UNPCUIComponent : public UPawnUIComponent
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable)
		void RegisterNPCDrawnWidget(URPGWidgetBase* InWidgetToRegister);

	UFUNCTION(BlueprintCallable)
		void RemoveNPCDrawnWidgetsIfAny();

private:
	TArray<URPGWidgetBase*> EnemyDrawnWidgets;
};
