// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MessageBox/RPGMessageBox.h"
#include "Components/TextBlock.h"
#include "GameInstance/RPGGameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Manager/SoundManager.h"

void URPGMessageBox::NativeConstruct()
{
	Super::NativeConstruct();

	MessageText->SetText(Message);

	USoundManager::Get()->Play(PopUpSound);
}

void URPGMessageBox::OnConfirm()
{
	URPGGameInstance* GI = URPGCoreFunctionLibrary::GetRPGGameInstance(GetWorld());
	
	if (GI)
	{
		TSoftObjectPtr<UWorld> WorldToEnter = GI->GetGameLevelByTag(GameLevelTag);

		UGameplayStatics::OpenLevelBySoftObjectPtr(GetWorld(), WorldToEnter);

		RemoveFromParent();
	}
}

void URPGMessageBox::OnCancel()
{
	RemoveFromParent();
}