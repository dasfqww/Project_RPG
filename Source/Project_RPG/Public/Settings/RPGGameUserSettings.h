// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameUserSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnSoundSettingsChanged);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	URPGGameUserSettings();

	static URPGGameUserSettings* GetRPGGameUserSettings();

	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float MasterVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float BGMVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	float EffectVolume = 1.0f;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bMasterMuted = false;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bBGMMuted = false;

	UPROPERTY(Config, BlueprintReadWrite, Category = "Audio")
	bool bEffectMuted = false;

	FOnSoundSettingsChanged OnSoundSettingsChanged;

	float GetVolume(ESoundType Type) const;
	void SetVolume(ESoundType Type, float InVolume);

	bool IsMuted(ESoundType Type) const;
	void SetMute(ESoundType Type, bool bMute);
};
