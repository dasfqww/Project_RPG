// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

//#include "CoreMinimal.h"
#include "Components/ControllerComponent.h"
#include "RPGItemComponent.generated.h"

class URPGItemBase;
class ARPGPickUpBase;
class URPGInventoryComponent;

UCLASS()
class PROJECT_RPG_API URPGItemComponent : public UControllerComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	URPGItemComponent(const FObjectInitializer& ObjectInitializer);

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

		
};
