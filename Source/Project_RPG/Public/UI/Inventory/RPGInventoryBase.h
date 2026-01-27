// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "Type/RPGStructTypes.h"
#include "RPGInventoryBase.generated.h"

class ARPGPickUpBase;
class URPGItemBase;
class URPGHoverItem;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGInventoryBase : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual FSlotAvailabilityResult HasSpaceForItem(ARPGPickUpBase* ItemPickup)
		const {return FSlotAvailabilityResult();}

	virtual void OnItemHovered(URPGItemBase* Item) {}
	virtual void OnItemUnHovered() {}
	virtual bool HasHoverItem() const { return false; }
	virtual URPGHoverItem* GetHoverItem() const { return nullptr; }
};
