#include "RPGSoundOptionsRuntime.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Settings/RPGGameUserSettings.h"
#include "RPGOptionsPersistence.h"
#include "Manager/SoundManager.h"
#include "Manager/RPGManagerContext.h"
#include "Type/RPGStructTypes.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    bool IsSoundChannel(ESoundType Type)
    {
        return Type == ESoundType::BGM || Type == ESoundType::Effect;
    }

    float NormalizeVolume(float Value)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, 1.f) : 1.f;
    }

    FRPGSoundSettings NormalizeSoundSettings(const FRPGSoundSettings& Source)
    {
        FRPGSoundSettings Result(Source);
        Result.MasterVolume = NormalizeVolume(Source.MasterVolume);
        Result.BGMVolume = NormalizeVolume(Source.BGMVolume);
        Result.EffectVolume = NormalizeVolume(Source.EffectVolume);
        return Result;
    }

    FRPGSoundSettings FromSoundSaveData(const FSoundSaveData& Saved)
    {
        FRPGSoundSettings Result;
        Result.MasterVolume = Saved.MasterVolume;
        Result.bMasterMuted = Saved.bMasterMuted;
        if (const float* Volume = Saved.Volumes.Find(TEXT("BGM"))) { Result.BGMVolume = *Volume; }
        if (const float* Volume = Saved.Volumes.Find(TEXT("Effect"))) { Result.EffectVolume = *Volume; }
        Result.bBGMMuted = Saved.Mutes.FindRef(TEXT("BGM"));
        Result.bEffectMuted = Saved.Mutes.FindRef(TEXT("Effect"));
        return NormalizeSoundSettings(Result);
    }

    FSoundSaveData MakeSoundSaveData(const FRPGSoundSettings& Settings)
    {
        FSoundSaveData Saved;
        Saved.MasterVolume = Settings.MasterVolume;
        Saved.bMasterMuted = Settings.bMasterMuted;
        Saved.Volumes.Add(TEXT("BGM"), Settings.BGMVolume);
        Saved.Volumes.Add(TEXT("Effect"), Settings.EffectVolume);
        Saved.Mutes.Add(TEXT("BGM"), Settings.bBGMMuted);
        Saved.Mutes.Add(TEXT("Effect"), Settings.bEffectMuted);
        return Saved;
    }

    FRPGSoundSettings ReadCurrentSoundSettings(const URPGGameUserSettings* Settings = nullptr)
    {
        FRPGSoundSettings Data;
        if (!Settings) { Settings = URPGGameUserSettings::GetRPGGameUserSettings(); }
        if (Settings)
        {
            Data.MasterVolume = Settings->MasterVolume;
            Data.BGMVolume = Settings->BGMVolume;
            Data.EffectVolume = Settings->EffectVolume;
            Data.bMasterMuted = Settings->bMasterMuted;
            Data.bBGMMuted = Settings->bBGMMuted;
            Data.bEffectMuted = Settings->bEffectMuted;
        }
        else
        {
            FSoundSaveData Saved;
            if (RPGOptionsPersistence::LoadSound(Saved)) { Data = FromSoundSaveData(Saved); }
        }
        return NormalizeSoundSettings(Data);
    }

    bool IsLiveSoundOwner(const UObject* Object)
    {
        return IsValid(Object) && !Object->IsUnreachable()
            && !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    FText SoundRequestBusy()
    {
        return NSLOCTEXT("RPGSoundOptions", "RequestBusy", "사운드 설정을 변경 중입니다. 잠시 후 다시 시도해 주세요.");
    }

    FText SoundApplyUnavailable()
    {
        return NSLOCTEXT("RPGSoundOptions", "ApplyUnavailable", "사운드 설정을 적용할 준비가 되지 않았습니다.");
    }

    FText SoundSettingsChanged()
    {
        return NSLOCTEXT("RPGSoundOptions", "SettingsChanged", "사운드 설정이나 재생 월드가 변경되었습니다. 현재 값을 확인한 뒤 다시 시도해 주세요.");
    }

    bool ValidateSoundSettings(const FRPGSoundSettings& Requested, FText& OutError)
    {
        if (FMath::IsFinite(Requested.MasterVolume) && FMath::IsFinite(Requested.BGMVolume)
            && FMath::IsFinite(Requested.EffectVolume)) { return true; }
        OutError = NSLOCTEXT("RPGSoundOptions", "InvalidSaveVolume", "올바른 볼륨을 선택해 주세요.");
        return false;
    }

    // Pins last only for this synchronous request; no owner survives a feature/world transition.
    struct FSoundOptionsRequest
    {
        explicit FSoundOptionsRequest(const UObject* InContext, UWorld* InitializingWorld = nullptr)
            : Context(IsLiveSoundOwner(InContext) ? const_cast<UObject*>(InContext) : nullptr)
            , bHadContext(InContext != nullptr)
        {
            if (!IsInGameThread() || (bHadContext && !Context.IsValid())) { return; }
            Manager.Reset(USoundManager::Get(InContext));
            if (!IsLiveSoundOwner(Manager.Get()) || Manager->HasAnyFlags(RF_ClassDefaultObject)) { return; }
            Instance.Reset(Manager->GetGameInstance());
            World.Reset(InitializingWorld ? InitializingWorld : Manager->GetPlaybackWorld());
            if (!IsLiveSoundOwner(Instance.Get()) || !IsLiveSoundOwner(World.Get())) { return; }
            Settings.Reset(URPGGameUserSettings::GetRPGGameUserSettings());
        }

        bool IsCurrent(bool bRequirePlaybackWorld = true) const
        {
            return IsInGameThread() && (!bHadContext || IsLiveSoundOwner(Context.Get()))
                && IsLiveSoundOwner(Settings.Get()) && IsLiveSoundOwner(Manager.Get())
                && !Manager->HasAnyFlags(RF_ClassDefaultObject)
                && IsLiveSoundOwner(Instance.Get()) && !Instance->IsDedicatedServerInstance()
                && IsLiveSoundOwner(World.Get()) && World->IsGameWorld() && !World->bIsTearingDown
                && World->GetNetMode() != NM_DedicatedServer
                && World->GetGameInstance() == Instance.Get() && Manager->GetGameInstance() == Instance.Get()
                && URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get()
                && (!bHadContext || RPGManagerContext::ResolveWorld(Context.Get()) == World.Get())
                && USoundManager::Get(Context.Get()) == Manager.Get()
                && (!bRequirePlaybackWorld || Manager->GetPlaybackWorld() == World.Get());
        }

        bool Matches(const FRPGSoundSettings& Snapshot) const
        {
            // Compare raw fields so invalid values cannot be hidden by UI normalization.
            return IsCurrent() && Settings->MasterVolume == Snapshot.MasterVolume
                && Settings->BGMVolume == Snapshot.BGMVolume && Settings->EffectVolume == Snapshot.EffectVolume
                && Settings->bMasterMuted == Snapshot.bMasterMuted && Settings->bBGMMuted == Snapshot.bBGMMuted
                && Settings->bEffectMuted == Snapshot.bEffectMuted;
        }

        TStrongObjectPtr<UObject> Context;
        const bool bHadContext;
        TStrongObjectPtr<USoundManager> Manager;
        TStrongObjectPtr<UGameInstance> Instance;
        TStrongObjectPtr<UWorld> World;
        TStrongObjectPtr<URPGGameUserSettings> Settings;
    };

    bool ApplySoundRequest(const FSoundOptionsRequest& Request, const FRPGSoundSettings& Snapshot, FText& OutError)
    {
        if (!Request.IsCurrent())
        {
            OutError = SoundApplyUnavailable();
            return false;
        }
        if (!Request.Manager->ApplySoundSettings(Snapshot))
        {
            OutError = Request.IsCurrent() ? SoundApplyUnavailable() : SoundSettingsChanged();
            return false;
        }
        if (!Request.Matches(Snapshot))
        {
            OutError = SoundSettingsChanged();
            return false;
        }
        return true;
    }
}

FRPGSoundSettings FRPGSoundOptionsRuntime::GetCurrentSettings() const
{
    return ReadCurrentSoundSettings();
}

bool FRPGSoundOptionsRuntime::TryApplySettings(const FRPGSoundSettings& Requested,
    const UObject* Context, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (!IsInGameThread()) { OutError = SoundApplyUnavailable(); return false; }
    if (bSoundRequestActive) { OutError = SoundRequestBusy(); return false; }
    TGuardValue<bool> RequestGuard(bSoundRequestActive, true);
    // Capture before settings lookup, which can lazily initialize the engine's settings object.
    const FRPGSoundSettings Snapshot(Requested);
    if (!ValidateSoundSettings(Snapshot, OutError)) { return false; }
    const FSoundOptionsRequest Request(Context);
    return ApplySoundRequest(Request, NormalizeSoundSettings(Snapshot), OutError);
}

bool FRPGSoundOptionsRuntime::TrySaveSettings(const FRPGSoundSettings& Requested,
    const UObject* Context, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (!IsInGameThread()) { OutError = SoundApplyUnavailable(); return false; }
    if (bSoundRequestActive) { OutError = SoundRequestBusy(); return false; }
    TGuardValue<bool> RequestGuard(bSoundRequestActive, true);
    const FRPGSoundSettings RequestedSnapshot(Requested);
    if (!ValidateSoundSettings(RequestedSnapshot, OutError)) { return false; }
    const FRPGSoundSettings Snapshot = NormalizeSoundSettings(RequestedSnapshot);
    const FSoundOptionsRequest Request(Context);
    if (!ApplySoundRequest(Request, Snapshot, OutError)) { return false; }

    // JSON and INI must use the same applied values and owner captured at entry.
    const FSoundSaveData SavedData = MakeSoundSaveData(ReadCurrentSoundSettings(Request.Settings.Get()));
    if (!Request.Matches(Snapshot)) { OutError = SoundSettingsChanged(); return false; }
    if (!RPGOptionsPersistence::SaveSound(SavedData, OutError)) { return false; }
    if (!Request.Matches(Snapshot)) { OutError = SoundSettingsChanged(); return false; }
    Request.Settings->SaveSettings();
    if (!Request.Matches(Snapshot)) { OutError = SoundSettingsChanged(); return false; }
    return true;
}

void FRPGSoundOptionsRuntime::UpdateSettings(const UObject* Context,
    TFunctionRef<void(FRPGSoundSettings&)> Update)
{
    if (!IsInGameThread() || bSoundRequestActive) { return; }
    TGuardValue<bool> RequestGuard(bSoundRequestActive, true);
    const FSoundOptionsRequest Request(Context);
    if (!Request.IsCurrent()) { return; }
    FRPGSoundSettings Updated = ReadCurrentSoundSettings(Request.Settings.Get());
    Update(Updated);
    FText ApplyError;
    ApplySoundRequest(Request, Updated, ApplyError);
}

void FRPGSoundOptionsRuntime::SetMasterVolume(float Value, const UObject* Context)
{
    if (!FMath::IsFinite(Value)) { return; }
    UpdateSettings(Context, [Value](FRPGSoundSettings& Settings) { Settings.MasterVolume = NormalizeVolume(Value); });
}

void FRPGSoundOptionsRuntime::SetMasterMute(bool bMute, float /*CurrentVolume*/, const UObject* Context)
{
    UpdateSettings(Context, [bMute](FRPGSoundSettings& Settings) { Settings.bMasterMuted = bMute; });
}

void FRPGSoundOptionsRuntime::SetChannelVolume(ESoundType Type, float Value, const UObject* Context)
{
    if (!IsSoundChannel(Type) || !FMath::IsFinite(Value)) { return; }
    UpdateSettings(Context, [Type, Value](FRPGSoundSettings& Settings)
    {
        if (Type == ESoundType::BGM) { Settings.BGMVolume = NormalizeVolume(Value); }
        else { Settings.EffectVolume = NormalizeVolume(Value); }
    });
}

void FRPGSoundOptionsRuntime::SetChannelMute(ESoundType Type, bool bMute, float /*CurrentVolume*/,
    const UObject* Context)
{
    if (!IsSoundChannel(Type)) { return; }
    UpdateSettings(Context, [Type, bMute](FRPGSoundSettings& Settings)
    {
        if (Type == ESoundType::BGM) { Settings.bBGMMuted = bMute; }
        else { Settings.bEffectMuted = bMute; }
    });
}

void FRPGSoundOptionsRuntime::RestoreDefaultSettings(const UObject* Context)
{
    FText ApplyError;
    TryApplySettings(FRPGSoundSettings(), Context, ApplyError);
}

bool FRPGSoundOptionsRuntime::InitializeForWorld(UWorld* World)
{
    if (!IsInGameThread() || bSoundRequestActive || !IsValid(World) || !World->IsGameWorld()
        || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer) { return false; }
    TGuardValue<bool> RequestGuard(bSoundRequestActive, true);
    const FSoundOptionsRequest Request(World, World);
    if (!Request.IsCurrent(false)) { return false; }

    // Each destination world needs its mixes; the settings JSON is imported only once.
    Request.Manager->Init(World);
    if (!Request.IsCurrent()) { return false; }
    if (Request.Settings->bHasInitializedSoundOptions) { return true; }
    FRPGSoundSettings Data = ReadCurrentSoundSettings(Request.Settings.Get());
    FSoundSaveData Saved;
    if (RPGOptionsPersistence::LoadSound(Saved)) { Data = FromSoundSaveData(Saved); }
    if (!Request.IsCurrent()) { return false; }
    Request.Settings->bHasInitializedSoundOptions = true;
    FText ApplyError;
    if (!ApplySoundRequest(Request, Data, ApplyError))
    {
        Request.Settings->bHasInitializedSoundOptions = false;
        return false;
    }
    return true;
}

void FRPGSoundOptionsRuntime::RestoreSavedSettings(const UObject* Context)
{
    if (!IsInGameThread() || bSoundRequestActive) { return; }
    TGuardValue<bool> RequestGuard(bSoundRequestActive, true);
    const FSoundOptionsRequest Request(Context);
    if (!Request.IsCurrent()) { return; }
    FSoundSaveData Data;
    if (RPGOptionsPersistence::LoadSound(Data))
    {
        FText ApplyError;
        ApplySoundRequest(Request, FromSoundSaveData(Data), ApplyError);
    }
}
