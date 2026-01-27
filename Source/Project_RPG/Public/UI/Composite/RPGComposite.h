// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Composite/RPGCompositeBase.h"
#include "RPGComposite.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGComposite : public URPGCompositeBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;
	virtual void ApplyFunction(FuncType Function) override;
	virtual void Collapse() override;

private:
	UPROPERTY()
	TArray<TObjectPtr<URPGCompositeBase>> Children;
};
