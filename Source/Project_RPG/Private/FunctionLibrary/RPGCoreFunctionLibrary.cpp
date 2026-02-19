// Fill out your copyright notice in the Description page of Project Settings.

#include "FunctionLibrary/RPGCoreFunctionLibrary.h"
#include "GameInstance/RPGGameInstance.h"
#include "Type/RPGCountDownAction.h"
#include "Kismet/GameplayStatics.h"

void URPGCoreFunctionLibrary::CountDown(const UObject* WorldContextObject, float TotalTime, float UpdateInterval,
	float& OutRemainingTime, ERPGCountDownActionInput CountDownInput,
	UPARAM(DisplayName = "Output") ERPGCountDownActionOutput& CountDownOutput, FLatentActionInfo LatentInfo)
{
	UWorld* World = nullptr;
	if (GEngine)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	}

	if (!World) return;

	FLatentActionManager& LatentActionManager = World->GetLatentActionManager();
	FRPGCountDownAction* FoundAction = LatentActionManager.FindExistingAction<FRPGCountDownAction>(LatentInfo.CallbackTarget, LatentInfo.UUID);

	if (CountDownInput == ERPGCountDownActionInput::Start)
	{
		if (!FoundAction)
		{
			LatentActionManager.AddNewAction(
				LatentInfo.CallbackTarget,
				LatentInfo.UUID,
				new FRPGCountDownAction(TotalTime, UpdateInterval, OutRemainingTime, CountDownOutput, LatentInfo)
			);
		}
	}
	else if (CountDownInput == ERPGCountDownActionInput::Cancel)
	{
		if (FoundAction)
		{
			FoundAction->CancelAction();
		}
	}
}

URPGGameInstance* URPGCoreFunctionLibrary::GetRPGGameInstance(const UObject* WorldContextObject)
{
	if (GEngine)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			return World->GetGameInstance<URPGGameInstance>();
		}
	}
	return nullptr;
}

void URPGCoreFunctionLibrary::ToggleInputMode(const UObject* WorldContextObject, ERPGInputMode InInputMode)
{
	APlayerController* PlayerController = nullptr;
	if (GEngine)
	{
		if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			PlayerController = World->GetFirstPlayerController();
		}
	}

	if (!PlayerController) return;

	if (InInputMode == ERPGInputMode::GameOnly)
	{
		FInputModeGameOnly GameOnlyMode;
		PlayerController->SetInputMode(GameOnlyMode);
		PlayerController->bShowMouseCursor = false;
	}
	else if (InInputMode == ERPGInputMode::UIOnly)
	{
		FInputModeUIOnly UIOnlyMode;
		PlayerController->SetInputMode(UIOnlyMode);
		PlayerController->bShowMouseCursor = true;
	}
}

FString URPGCoreFunctionLibrary::FormatTimeToMMSS(float InSeconds)
{
	int32 TotalSeconds = FMath::FloorToInt(InSeconds);
	return FString::Printf(TEXT("%02d:%02d"), TotalSeconds / 60, TotalSeconds % 60);
}
