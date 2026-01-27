// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Button/RPGCustomButton.h"
#include "Sound/SoundBase.h"
#include "Manager/SoundManager.h"

#include "RPGDebugHelper.h"

URPGCustomButton::URPGCustomButton()
{
	
}

void URPGCustomButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	//OnPressed.AddDynamic(this, &ThisClass::OnPressedCallback);
	//OnHovered.AddDynamic(this, &ThisClass::OnHoveredCallback);
}

void URPGCustomButton::PostLoad()
{
	Super::PostLoad();

	
}

void URPGCustomButton::OnHoveredCallback()
{
	USoundManager::Get()->Play(HoverSound);
}

void URPGCustomButton::OnPressedCallback()
{
	USoundManager::Get()->Play(PressedSound);
}
