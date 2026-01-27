// Fill out your copyright notice in the Description page of Project Settings.

#include "Manager/SoundManager.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameInstance/RPGGameInstance.h"
#include "Settings/RPGGameUserSettings.h"

#include "RPGDebugHelper.h"

USoundManager* USoundManager::Instance = nullptr;

USoundManager* USoundManager::Get()
{
	if (!Instance)
	{
		Instance = NewObject<USoundManager>();
		Instance->AddToRoot();
	}

	return Instance;
}

void USoundManager::Init(UWorld* WorldContext)
{
	WorldRef = WorldContext;

	// Apply initial settings from GameUserSettings
	UpdateEngineMasterSoundMix();
	UpdateEngineSoundMix(ESoundType::BGM);
	UpdateEngineSoundMix(ESoundType::Effect);
}

void USoundManager::Play(USoundBase* Sound, ESoundType Type, float Pitch)
{
	if (!Sound)
		return;

	if (Type == ESoundType::BGM)
	{
		UGameplayStatics::SpawnSound2D(WorldRef, Sound, Pitch);
	}

	else if (Type == ESoundType::Effect)
	{
		UGameplayStatics::PlaySound2D(WorldRef, Sound, Pitch);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid sound type: %d"), (int32)Type);
	}
}

void USoundManager::Play(USoundBase* Sound, FVector Location, float Pitch)
{
	if (!Sound || !WorldRef)
		return;

	UGameplayStatics::PlaySoundAtLocation(
		WorldRef,
		Sound,
		Location,
		FRotator::ZeroRotator,
		Pitch
	);
}

void USoundManager::SetMasterVolume(float Volume)
{
	if (URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
	{
		Settings->MasterVolume = Volume;
		Settings->ApplySettings(false);
	}
	UpdateEngineMasterSoundMix();
}

void USoundManager::SetVolume(ESoundType Type, float Volume)
{
	if (URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
	{
		Settings->SetVolume(Type, Volume);
		Settings->ApplySettings(false);
	}
	UpdateEngineSoundMix(Type);
}

void USoundManager::SetMasterMute(bool bMute, float CurrentValue)
{
	if (URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
	{
		Settings->bMasterMuted = bMute;
		Settings->ApplySettings(false);
	}
	UpdateEngineMasterSoundMix();
}

void USoundManager::SetMute(ESoundType Type, bool bMute, float CurrentVolume)
{
	if (URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
	{
		Settings->SetMute(Type, bMute);
		Settings->ApplySettings(false);
	}
	UpdateEngineSoundMix(Type);
}

void USoundManager::UpdateEngineMasterSoundMix()
{
	URPGGameInstance* GI = Cast<URPGGameInstance>(UGameplayStatics::GetGameInstance(WorldRef));
	URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();

	if (GI && Settings)
	{
		const FSoundClassPair& Pair = GI->GetMasterSound();
		float NewVolume = Settings->bMasterMuted ? 0.f : Settings->MasterVolume;

		UGameplayStatics::SetSoundMixClassOverride(
			WorldRef,
			Pair.SoundMix,
			Pair.SoundClass,
			NewVolume,
			1.f,
			0.f
		);
		UGameplayStatics::PushSoundMixModifier(WorldRef, Pair.SoundMix);
	}
}

void USoundManager::UpdateEngineSoundMix(ESoundType Type)
{
	URPGGameInstance* GI = Cast<URPGGameInstance>(UGameplayStatics::GetGameInstance(WorldRef));
	URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();

	if (GI && Settings)
	{
		const FSoundClassPair* Pair = GI->GetSoundClassPair(Type);
		if (Pair)
		{
			// Check Master Mute as well
			bool bIsMuted = Settings->IsMuted(Type) || Settings->bMasterMuted;
			float NewVolume = bIsMuted ? 0.f : Settings->GetVolume(Type);

			UGameplayStatics::SetSoundMixClassOverride(
				WorldRef,
				Pair->SoundMix,
				Pair->SoundClass,
				NewVolume,
				1.f,
				0.f
			);
			UGameplayStatics::PushSoundMixModifier(WorldRef, Pair->SoundMix);
		}
	}
}

void USoundManager::Play(FString Path, ESoundType Type, float Pitch)
{
	USoundBase* Sound = GetOrLoadSound(Path, Type);
	Play(Sound, Type, Pitch);
}

USoundBase* USoundManager::GetOrLoadSound(FString Path, ESoundType Type)
{
	if (!Path.StartsWith(TEXT("Sound/")))
		Path = FString::Printf(TEXT("Sound/%s"), *Path);

	if (SoundClips.Contains(Path))
		return SoundClips[Path];

	USoundBase* LoadedSound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/%s.%s"), *Path, *FPaths::GetCleanFilename(Path)));

	if (!LoadedSound)
	{
		UE_LOG(LogTemp, Warning, TEXT("Sound Missing: %s"), *Path);
		return nullptr;
	}

	if (Type == ESoundType::Effect)
	{
		SoundClips.Add(Path, LoadedSound);
	}

	return LoadedSound;
}