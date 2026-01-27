// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UIManager.generated.h"

class URPGWidgetBase;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UUIManager : public UObject
{
	GENERATED_BODY()
public:
	static UUIManager* Get();

	void PushUI(URPGWidgetBase* NewUI);
	void PopUI();
	void RemoveUI(URPGWidgetBase* TargetUI);

	URPGWidgetBase* GetTopUI() const;

private:
	UPROPERTY()
	TArray<TObjectPtr<URPGWidgetBase>> PopUpUIStack;

	static UUIManager* Instance;

public:
	FORCEINLINE bool HasUIOpen() const { return PopUpUIStack.Num() > 0; }
};
