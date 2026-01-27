// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Option/GraphicOptionMenu.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Manager/DataManager.h"

#include "RPGDebugHelper.h"

UGraphicOptionMenu::UGraphicOptionMenu()
{
}

void UGraphicOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();

	// 콤보박스 기본 옵션 채우기
	if (ResolutionComboBox)
	{
		ResolutionComboBox->AddOption(TEXT("1080x720"));
		ResolutionComboBox->AddOption(TEXT("1440x960"));
		ResolutionComboBox->AddOption(TEXT("1920x1080"));
		ResolutionComboBox->SetSelectedIndex(0); // 기본값
	}

	if (WindowModeComboBox)
	{
		WindowModeComboBox->AddOption(TEXT("Fullscreen"));
		WindowModeComboBox->AddOption(TEXT("Windowed Fullscreen"));
		WindowModeComboBox->AddOption(TEXT("Windowed"));
		WindowModeComboBox->SetSelectedIndex(0);
	}

	InitializeOptions();
}

void UGraphicOptionMenu::InitializeOptions()
{
	FGraphicSaveData SaveData;
	if (UDataManager::Get()->LoadGraphicOptionsFromJson(SaveData))
	{
		ResolutionComboBox->SetSelectedOption(SaveData.Resolution);
		WindowModeComboBox->SetSelectedOption(SaveData.WindowMode);

		VSyncCheckBox->SetIsChecked(SaveData.bVSync);
	}
}

void UGraphicOptionMenu::OnApplyButtonClicked()
{
	FGraphicSaveData SaveData;

	// 1. 현재 유저 세팅 불러오기
	UGameUserSettings* Settings = GEngine->GetGameUserSettings();
	if (!Settings) return;

	// 2. 해상도 파싱 (예: "1920x1080" 형태)
	FString ResolutionStr = ResolutionComboBox->GetSelectedOption(); // "1920x1080"
	FString LeftStr, RightStr;
	if (ResolutionStr.Split("x", &LeftStr, &RightStr))
	{
		int32 Width = FCString::Atoi(*LeftStr);
		int32 Height = FCString::Atoi(*RightStr);
		Settings->SetScreenResolution(FIntPoint(Width, Height));
	}

	SaveData.Resolution = ResolutionStr;

	// 3. 창 모드 설정
	FString WindowModeStr = WindowModeComboBox->GetSelectedOption(); // "Fullscreen", "Windowed", etc.
	EWindowMode::Type WindowMode = EWindowMode::Windowed;
	if (WindowModeStr == "Fullscreen")
		WindowMode = EWindowMode::Fullscreen;
	else if (WindowModeStr == "WindowedFullscreen")
		WindowMode = EWindowMode::WindowedFullscreen;
	else if(WindowModeStr=="Windowed")
		WindowMode = EWindowMode::Windowed;

	Settings->SetFullscreenMode(WindowMode);

	SaveData.WindowMode = WindowModeStr;

	// 4. VSync
	bool bVSyncEnabled = VSyncCheckBox->IsChecked();
	Settings->SetVSyncEnabled(bVSyncEnabled);

	SaveData.bVSync = bVSyncEnabled;

	// 5. 실제 적용
	Settings->ApplySettings(false); // true면 로딩 화면 나올 수 있음

	// 선택적으로 저장
	Settings->SaveSettings();

	UDataManager::Get()->SaveOptionToJson(SaveData, "graphic_options.json");

	Debug::Print("Graphic Option is applied..");
}
