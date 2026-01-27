// Fill out your copyright notice in the Description page of Project Settings.


#include "Settings/RPGGameUserSettings.h"

URPGGameUserSettings::URPGGameUserSettings()
{
	MasterVolume = 1.0f;
	BGMVolume = 1.0f;
	EffectVolume = 1.0f;
	bMasterMuted = false;
	bBGMMuted = false;
	bEffectMuted = false;
}

URPGGameUserSettings* URPGGameUserSettings::GetRPGGameUserSettings()
{
	return Cast<URPGGameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

void URPGGameUserSettings::ApplySettings(bool bCheckForCommandLineOverrides)
{
	Super::ApplySettings(bCheckForCommandLineOverrides);

	OnSoundSettingsChanged.Broadcast();
}

float URPGGameUserSettings::GetVolume(ESoundType Type) const
{
	switch (Type)
	{
	case ESoundType::BGM:
		return BGMVolume;
	case ESoundType::Effect:
		return EffectVolume;
	default:
		return 1.0f;
	}
}

void URPGGameUserSettings::SetVolume(ESoundType Type, float InVolume)
{
	switch (Type)
	{
	case ESoundType::BGM:
		BGMVolume = InVolume;
		break;
	case ESoundType::Effect:
		EffectVolume = InVolume;
		break;
	}
}

bool URPGGameUserSettings::IsMuted(ESoundType Type) const
{
	switch (Type)
	{
	case ESoundType::BGM:
		return bBGMMuted;
	case ESoundType::Effect:
		return bEffectMuted;
	default:
		return false;
	}
}

void URPGGameUserSettings::SetMute(ESoundType Type, bool bMute)
{
	switch (Type)
	{
	case ESoundType::BGM:
		bBGMMuted = bMute;
		break;
	case ESoundType::Effect:
		bEffectMuted = bMute;
		break;
	}
}