// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Content/RPGBossBattleInfoWidget.h"
#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "Components/TextBlock.h"
#include "GameMode/RPGBossBattleGameMode.h"

#include "RPGDebugHelper.h"

void URPGBossBattleInfoWidget::NativeConstruct()
{
	Super::NativeConstruct();


}

void URPGBossBattleInfoWidget::UpdateBattleLimitTimeText(UTextBlock* TextBlock, float InRemainingTime)
{
	ARPGBossBattleGameMode* GameMode = Cast<ARPGBossBattleGameMode>(GetWorld()->GetAuthGameMode());
	if (GameMode)
	{
		if (GameMode->GetCurrentBossBattleState()==EBossBattleState::Defeated||
			GameMode->GetCurrentBossBattleState()==EBossBattleState::Victory)
		{
			return;
		}
	}

	FString TimeString = URPGCoreFunctionLibrary::FormatTimeToMMSS(InRemainingTime);
	FString FinalString="Limit Time: " + TimeString;
	TextBlock->SetText(FText::FromString(TimeString));

	//Debug::Print(TimeString);
}

void URPGBossBattleInfoWidget::OnRestart()
{

}

void URPGBossBattleInfoWidget::OnExit()
{

}
