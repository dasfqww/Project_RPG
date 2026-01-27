// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Type/RPGEnumTypes.h"
#include "SoundManager.generated.h"

class UAudioComponent;
class ASoundManagerActor;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API USoundManager : public UObject
{
	GENERATED_BODY()
public:

    static USoundManager* Get();

    void Init(UWorld* WorldContext);

    void Play(USoundBase* Sound, ESoundType Type = ESoundType::Effect, float Pitch = 1.0f);
    void Play(FString Path, ESoundType Type = ESoundType::Effect, float Pitch = 1.0f);
    void Play(USoundBase* Sound, FVector Location, float Pitch = 1.0f);
   // void Play(FString Path, FVector Location, float Volume = 1.0f, float Pitch = 1.0f);
    
    void SetMasterVolume(float Volume);
    void SetVolume(ESoundType Type, float Volume);

    void SetMasterMute(bool bMute, float CurrentValue);
    void SetMute(ESoundType Type, bool bMute, float CurrentVolume);

private:
    static USoundManager* Instance;

    UPROPERTY()
    TMap<FString, USoundBase*> SoundClips;

    UPROPERTY()
    TMap<ESoundType, UAudioComponent*> AudioComponents;
    
    // ���� Ÿ�Ժ��� ���� ���� ����
    UPROPERTY()
    TObjectPtr<UWorld> WorldRef;

   /* UPROPERTY()
    TObjectPtr<ASoundManagerActor> SoundActor;*/

    USoundBase* GetOrLoadSound(FString Path, ESoundType Type);

    void UpdateEngineSoundMix(ESoundType Type);
    void UpdateEngineMasterSoundMix();
};
