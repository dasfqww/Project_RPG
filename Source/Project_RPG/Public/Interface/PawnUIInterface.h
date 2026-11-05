// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PawnUIInterface.generated.h"

class UPawnUIComponent;

UINTERFACE(MinimalAPI)
class UPawnUIInterface : public UInterface
{
	GENERATED_BODY()
};

/** Common UI capability; specialized views are selected by each consumer. */
class PROJECT_RPG_API IPawnUIInterface
{
	GENERATED_BODY()

	// Every pawn exposes only its common UI component. Consumers select optional specialized views.
public:
	virtual UPawnUIComponent* GetPawnUIComponent() const = 0;
};
