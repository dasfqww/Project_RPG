// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Composite/RPGCompositeBase.h"
#include "RPGLeaf.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGLeaf : public URPGCompositeBase
{
	GENERATED_BODY()
public:
	virtual void ApplyFunction(FuncType Function);
};
