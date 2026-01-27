// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Option/RPGOptionMenu.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "Manager/SoundManager.h"

URPGOptionMenu::URPGOptionMenu()
{
}

void URPGOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();

	InitButtonMap();
}

void URPGOptionMenu::InitButtonMap()
{
	TabButtonMap.Add(ESettingTabType::Graphics, GraphicOptionButton);
	TabButtonMap.Add(ESettingTabType::Sound, SoundOptionButton);

	TabIndexMap.Add(ESettingTabType::Graphics, 0);
	TabIndexMap.Add(ESettingTabType::Sound, 1);

	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (Pair.Value)
		{
			Pair.Value->OnHovered.AddDynamic(this, &URPGOptionMenu::OnTabButtonHovered);
			Pair.Value->OnClicked.AddDynamic(this, &URPGOptionMenu::OnTabButtonClicked);			
		}
	}
}

void URPGOptionMenu::OnTabButtonHovered()
{
	USoundManager::Get()->Play(HoverButtonSound);
}

void URPGOptionMenu::OnTabButtonClicked()
{
	// 어떤 버튼이 눌렸는지 확인
	for (const TPair<ESettingTabType, TObjectPtr<UButton>>& Pair : TabButtonMap)
	{
		if (Pair.Value->HasKeyboardFocus()) // 또는 IsHovered()로도 가능
		{
			if (int32* Index = TabIndexMap.Find(Pair.Key))
			{
				USoundManager::Get()->Play(ClickButtonSound);
				TabSwitcher->SetActiveWidgetIndex(*Index);
				break;
			}
		}
	}
}