// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "RPGCompositeBase.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGCompositeBase : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void Collapse();
	void Expand();

	using FuncType = TFunction<void(URPGCompositeBase*)>;
	virtual void ApplyFunction(FuncType Function){}

private:
	UPROPERTY(EditAnywhere, Category = "Inventory")
	FGameplayTag FragmentTag;

public:
	FORCEINLINE	FGameplayTag GetFragmentTag() const { return FragmentTag; }
	FORCEINLINE void SetFragmentTag(const FGameplayTag& Tag) { FragmentTag = Tag; }
};
