// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/PopUp/RPGDialogueWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void URPGDialogueWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ExitButton->OnClicked.AddDynamic(this, &ThisClass::RemoveFromParent);
}

void URPGDialogueWidget::SetDialogueText(const FText& NPCName, const FText& DialogueText)
{
	if (IsValid(NPCNameTextBlock)&&IsValid(DialogueTextBlock))
	{
		NPCNameTextBlock->SetText(NPCName);
		DialogueTextBlock->SetText(DialogueText);
	}
	
}
