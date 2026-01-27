// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGDialogueWidget.generated.h"

class UTextBlock;
class UButton;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGDialogueWidget : public URPGWidgetBase
{
	GENERATED_BODY()
public:
	virtual void NativeOnInitialized() override;

	void SetDialogueText(const FText& NPCName, const FText& DialogueText);

private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> NPCNameTextBlock;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> DialogueTextBlock;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> ExitButton;

};
