#include "Manager/SoundManager.h"

#include "Kismet/GameplayStatics.h"
#include "GameInstance/RPGGameInstance.h"
#include "Manager/RPGManagerContext.h"
#include "Settings/RPGGameUserSettings.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    FRPGSoundSettings CaptureSoundSettings(const URPGGameUserSettings& Settings)
    {
        const auto NormalizeVolume = [](float Value)
        {
            return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 1.f;
        };
        FRPGSoundSettings Result;
        Result.MasterVolume = NormalizeVolume(Settings.MasterVolume);
        Result.BGMVolume = NormalizeVolume(Settings.BGMVolume);
        Result.EffectVolume = NormalizeVolume(Settings.EffectVolume);
        Result.bMasterMuted = Settings.bMasterMuted;
        Result.bBGMMuted = Settings.bBGMMuted;
        Result.bEffectMuted = Settings.bEffectMuted;
        return Result;
    }
}

USoundManager* USoundManager::Get()
{
    return Get(nullptr);
}

USoundManager* USoundManager::Get(const UObject* Context)
{
    check(IsInGameThread());
    UGameInstance* Instance = RPGManagerContext::ResolveGameInstance(Context);
    USoundManager* Manager = IsValid(Instance) ? Instance->GetSubsystem<USoundManager>() : nullptr;
    return IsValid(Manager) && !Manager->bDeinitializing
        ? Manager : GetMutableDefault<USoundManager>();
}

void USoundManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bDeinitializing = false;
    FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);
}

void USoundManager::Deinitialize()
{
    bDeinitializing = true;
    FWorldDelegates::OnWorldCleanup.RemoveAll(this);
    ReleaseWorldState();
    SoundClips.Reset();
    Super::Deinitialize();
}

UWorld* USoundManager::GetPlaybackWorld() const
{
    if (!IsInGameThread() || IsUnreachable()
        || HasAnyFlags(RF_ClassDefaultObject | RF_BeginDestroyed | RF_FinishDestroyed)
        || bDeinitializing) { return nullptr; }
    UWorld* World = WorldRef.Get();
    UGameInstance* Instance = GetGameInstance();
    if (!IsValid(Instance) || Instance->IsUnreachable()
        || Instance->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)) { return nullptr; }
    if (!World) { World = Instance->GetWorld(); }
    return IsValid(World) && !World->IsUnreachable()
        && !World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
        && !World->bIsTearingDown && World->GetGameInstance() == Instance ? World : nullptr;
}

void USoundManager::Init(UWorld* WorldContext)
{
    if (!IsInGameThread() || HasAnyFlags(RF_ClassDefaultObject) || bDeinitializing) { return; }
    if (!IsValid(WorldContext) || WorldContext->bIsTearingDown
        || WorldContext->GetGameInstance() != GetGameInstance()) { return; }
    if (WorldRef.Get() != WorldContext)
    {
        ReleaseWorldState();
        WorldRef = WorldContext;
    }
    UpdateEngineMasterSoundMix();
    UpdateEngineSoundMix(ESoundType::BGM);
    UpdateEngineSoundMix(ESoundType::Effect);
}

void USoundManager::ReleaseWorldState()
{
    UWorld* World = WorldRef.Get();
    if (World)
    {
        for (const TWeakObjectPtr<USoundMix>& Reference : PushedSoundMixes)
        {
            if (USoundMix* Mix = Reference.Get())
            {
                UGameplayStatics::PopSoundMixModifier(World, Mix);
            }
        }
    }
    PushedSoundMixes.Reset();
    WorldRef.Reset();
}

void USoundManager::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
    if (WorldRef.Get() == World) { ReleaseWorldState(); }
}

void USoundManager::PushSoundMixOnce(UWorld* World, USoundMix* SoundMix)
{
    if (!World || !SoundMix) { return; }
    if (WorldRef.Get() != World)
    {
        ReleaseWorldState();
        WorldRef = World;
    }
    const TWeakObjectPtr<USoundMix> Reference(SoundMix);
    if (!PushedSoundMixes.Contains(Reference))
    {
        PushedSoundMixes.Add(Reference);
        UGameplayStatics::PushSoundMixModifier(World, SoundMix);
    }
}

void USoundManager::Play(USoundBase* Sound, ESoundType Type, float Pitch)
{
    UWorld* World = GetPlaybackWorld();
    if (!Sound || !World) { return; }
    if (Type == ESoundType::BGM)
    {
        UGameplayStatics::SpawnSound2D(World, Sound, Pitch);
    }
    else if (Type == ESoundType::Effect)
    {
        UGameplayStatics::PlaySound2D(World, Sound, Pitch);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid sound type: %d"), static_cast<int32>(Type));
    }
}

void USoundManager::Play(USoundBase* Sound, FVector Location, float Pitch)
{
    UWorld* World = GetPlaybackWorld();
    if (!Sound || !World) { return; }
    UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Pitch);
}

bool USoundManager::ApplySoundSettings(const FRPGSoundSettings& RequestedSettings)
{
    if (!IsInGameThread() || bApplyingSoundSettings) { return false; }
    // The caller may own these values; settings initialization and listeners may change them.
    FRPGSoundSettings Snapshot(RequestedSettings);
    if (!FMath::IsFinite(Snapshot.MasterVolume) || !FMath::IsFinite(Snapshot.BGMVolume)
        || !FMath::IsFinite(Snapshot.EffectVolume)) { return false; }
    Snapshot.MasterVolume = FMath::Clamp(Snapshot.MasterVolume, 0.f, 1.f);
    Snapshot.BGMVolume = FMath::Clamp(Snapshot.BGMVolume, 0.f, 1.f);
    Snapshot.EffectVolume = FMath::Clamp(Snapshot.EffectVolume, 0.f, 1.f);

    TStrongObjectPtr<UWorld> World(GetPlaybackWorld());
    if (!World.IsValid()) { return false; }
    TStrongObjectPtr<USoundManager> KeepAlive(this);
    TGuardValue<bool> ApplyingGuard(bApplyingSoundSettings, true);
    TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
    const auto IsCurrentOwner = [this, &World, &Settings]()
    {
        return GetPlaybackWorld() == World.Get() && Settings.IsValid() && !Settings->IsUnreachable()
            && !Settings->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
            && URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get();
    };
    if (!IsCurrentOwner()) { return false; }

    // Commit all values before updating mixes or notifying listeners.
    Settings->MasterVolume = Snapshot.MasterVolume;
    Settings->BGMVolume = Snapshot.BGMVolume;
    Settings->EffectVolume = Snapshot.EffectVolume;
    Settings->bMasterMuted = Snapshot.bMasterMuted;
    Settings->bBGMMuted = Snapshot.bBGMMuted;
    Settings->bEffectMuted = Snapshot.bEffectMuted;
    // A live change also establishes startup state before listeners can activate options again.
    Settings->bHasInitializedSoundOptions = true;

    UpdateEngineMasterSoundMix();
    if (!IsCurrentOwner()) { return false; }
    UpdateEngineSoundMix(ESoundType::BGM);
    if (!IsCurrentOwner()) { return false; }
    UpdateEngineSoundMix(ESoundType::Effect);
    if (!IsCurrentOwner()) { return false; }
    Settings->OnSoundSettingsChanged.Broadcast();
    return IsCurrentOwner() && Settings->MasterVolume == Snapshot.MasterVolume
        && Settings->BGMVolume == Snapshot.BGMVolume && Settings->EffectVolume == Snapshot.EffectVolume
        && Settings->bMasterMuted == Snapshot.bMasterMuted && Settings->bBGMMuted == Snapshot.bBGMMuted
        && Settings->bEffectMuted == Snapshot.bEffectMuted;
}

void USoundManager::SetMasterVolume(float Volume)
{
    if (!GetPlaybackWorld() || !FMath::IsFinite(Volume)) { return; }
    const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!Settings) { return; }
    FRPGSoundSettings Updated = CaptureSoundSettings(*Settings);
    Updated.MasterVolume = Volume;
    ApplySoundSettings(Updated);
}

void USoundManager::SetVolume(ESoundType Type, float Volume)
{
    if (!GetPlaybackWorld() || !FMath::IsFinite(Volume)
        || (Type != ESoundType::BGM && Type != ESoundType::Effect)) { return; }
    const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!Settings) { return; }
    FRPGSoundSettings Updated = CaptureSoundSettings(*Settings);
    if (Type == ESoundType::BGM) { Updated.BGMVolume = Volume; }
    else { Updated.EffectVolume = Volume; }
    ApplySoundSettings(Updated);
}

void USoundManager::SetMasterMute(bool bMute, float /*CurrentValue*/)
{
    if (!GetPlaybackWorld()) { return; }
    const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!Settings) { return; }
    FRPGSoundSettings Updated = CaptureSoundSettings(*Settings);
    Updated.bMasterMuted = bMute;
    ApplySoundSettings(Updated);
}

void USoundManager::SetMute(ESoundType Type, bool bMute, float /*CurrentVolume*/)
{
    if (!GetPlaybackWorld() || (Type != ESoundType::BGM && Type != ESoundType::Effect)) { return; }
    const URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!Settings) { return; }
    FRPGSoundSettings Updated = CaptureSoundSettings(*Settings);
    if (Type == ESoundType::BGM) { Updated.bBGMMuted = bMute; }
    else { Updated.bEffectMuted = bMute; }
    ApplySoundSettings(Updated);
}

void USoundManager::UpdateEngineMasterSoundMix()
{
    UWorld* World = GetPlaybackWorld();
    URPGGameInstance* Instance = Cast<URPGGameInstance>(GetGameInstance());
    URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!World || !Instance || !Settings) { return; }

    const FSoundClassPair& Pair = Instance->GetMasterSound();
    if (!Pair.SoundMix || !Pair.SoundClass) { return; }
    const float Volume = Settings->bMasterMuted ? 0.f : Settings->MasterVolume;
    UGameplayStatics::SetSoundMixClassOverride(
        World, Pair.SoundMix, Pair.SoundClass, Volume, 1.f, 0.f);
    PushSoundMixOnce(World, Pair.SoundMix);
}

void USoundManager::UpdateEngineSoundMix(ESoundType Type)
{
    UWorld* World = GetPlaybackWorld();
    URPGGameInstance* Instance = Cast<URPGGameInstance>(GetGameInstance());
    URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings();
    if (!World || !Instance || !Settings) { return; }

    const FSoundClassPair* Pair = Instance->GetSoundClassPair(Type);
    if (!Pair || !Pair->SoundMix || !Pair->SoundClass) { return; }
    const bool bMuted = Settings->IsMuted(Type) || Settings->bMasterMuted;
    const float Volume = bMuted ? 0.f : Settings->GetVolume(Type);
    UGameplayStatics::SetSoundMixClassOverride(
        World, Pair->SoundMix, Pair->SoundClass, Volume, 1.f, 0.f);
    PushSoundMixOnce(World, Pair->SoundMix);
}

void USoundManager::Play(FString Path, ESoundType Type, float Pitch)
{
    // Do not load or cache assets when a call has no live owner.
    if (!GetPlaybackWorld()) { return; }
    Play(GetOrLoadSound(Path, Type), Type, Pitch);
}

USoundBase* USoundManager::GetOrLoadSound(FString Path, ESoundType Type)
{
    if (!Path.StartsWith(TEXT("Sound/")))
    {
        Path = FString::Printf(TEXT("Sound/%s"), *Path);
    }
    if (USoundBase** Cached = SoundClips.Find(Path)) { return *Cached; }
    USoundBase* Sound = LoadObject<USoundBase>(nullptr,
        *FString::Printf(TEXT("/Game/%s.%s"), *Path, *FPaths::GetCleanFilename(Path)));
    if (!Sound)
    {
        UE_LOG(LogTemp, Warning, TEXT("Sound Missing: %s"), *Path);
        return nullptr;
    }
    if (Type == ESoundType::Effect) { SoundClips.Add(Path, Sound); }
    return Sound;
}
