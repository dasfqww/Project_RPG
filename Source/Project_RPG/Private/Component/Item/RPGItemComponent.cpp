// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Item/RPGItemComponent.h"
#include "Component/RPGInventoryComponent.h"

URPGItemComponent::URPGItemComponent(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

// Called when the game starts
void URPGItemComponent::BeginPlay()
{
	Super::BeginPlay();

	// ...
	
}


// Called every frame
void URPGItemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

