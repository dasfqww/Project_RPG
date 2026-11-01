#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Type/RPGEnumTypes.h"
#include "Settings/RPGSoundSettings.h"
#include "SoundManager.generated.h"

class UAudioComponent;
class USoundBase;
class USoundMix;

/** Sound cache and mix registrations owned by one game instance. */
UCLASS()
class PROJECT_RPG_API USoundManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** Game-thread access. Always returns a receiver; prefer Get(Context) for instance ownership. */
    static USoundManager* Get();
    /** Missing owners use a stateless receiver that ignores playback and settings requests. */
    static USoundManager* Get(const UObject* Context);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void Init(UWorld* WorldContext);
    /** Current live playback owner; returned only on the game thread. Does not initialize a world. */
    UWorld* GetPlaybackWorld() const;
    void Play(USoundBase* Sound, ESoundType Type = ESoundType::Effect, float Pitch = 1.0f);
    void Play(FString Path, ESoundType Type = ESoundType::Effect, float Pitch = 1.0f);
    void Play(USoundBase* Sound, FVector Location, float Pitch = 1.0f);

    /** Apply one complete audio snapshot and notify once, without graphics changes or disk writes. */
    bool ApplySoundSettings(const FRPGSoundSettings& RequestedSettings);

    void SetMasterVolume(float Volume);
    void SetVolume(ESoundType Type, float Volume);
    void SetMasterMute(bool bMute, float CurrentValue);
    void SetMute(ESoundType Type, bool bMute, float CurrentVolume);

private:
    UPROPERTY()
    TMap<FString, USoundBase*> SoundClips;

    TWeakObjectPtr<UWorld> WorldRef;
    TSet<TWeakObjectPtr<USoundMix>> PushedSoundMixes;
    bool bDeinitializing = true;
    bool bApplyingSoundSettings = false;

    void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    void ReleaseWorldState();
    void PushSoundMixOnce(UWorld* World, USoundMix* SoundMix);
    USoundBase* GetOrLoadSound(FString Path, ESoundType Type);
    void UpdateEngineSoundMix(ESoundType Type);
    void UpdateEngineMasterSoundMix();
};
