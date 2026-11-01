#include "UI/Option/RPGSoundOptionsRuntime.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "UI/CheckBox/RPGCheckBox.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGSoundOptionsBridge, Log, All);

FName IRPGSoundOptionsRuntime::GetFeatureName()
{
    static const FName Name(TEXT("RPGSoundOptionsRuntime"));
    return Name;
}

IRPGSoundOptionsRuntime* IRPGSoundOptionsRuntime::Get()
{
    check(IsInGameThread());
    IModularFeatures& Features = IModularFeatures::Get();
    const FName Name = GetFeatureName();
    if (!Features.IsModularFeatureAvailable(Name))
    {
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
    }
    if (Features.IsModularFeatureAvailable(Name))
    {
        return &Features.GetModularFeature<IRPGSoundOptionsRuntime>(Name);
    }
    UE_LOG(LogRPGSoundOptionsBridge, Error,
        TEXT("Sound options implementation is unavailable. Enable the RPGOptionsUI plugin."));
    return nullptr;
}

namespace
{
    bool IsSoundMenuChannel(ESoundType Type)
    {
        return Type == ESoundType::BGM || Type == ESoundType::Effect;
    }
}

void IRPGSoundOptionsRuntime::SynchronizeMenuControls(const FRPGSoundSettings& Settings,
    const FRPGSoundOptionsControls& Controls, const TMap<ESoundType, USlider*>& Sliders,
    const TMap<ESoundType, URPGCheckBox*>& Mutes)
{
    const FRPGSoundSettings Snapshot(Settings);
    const TMap<ESoundType, USlider*> SliderSnapshot(Sliders);
    const TMap<ESoundType, URPGCheckBox*> MuteSnapshot(Mutes);
    TStrongObjectPtr<USlider> MasterVolume(IsValid(Controls.MasterVolume) ? Controls.MasterVolume : nullptr);
    TStrongObjectPtr<UCheckBox> MasterMute(IsValid(Controls.MasterMute) ? Controls.MasterMute : nullptr);
    TArray<TStrongObjectPtr<USlider>> SliderReferences;
    TArray<TStrongObjectPtr<URPGCheckBox>> MuteReferences;
    for (const auto& Pair : SliderSnapshot)
    {
        if (IsValid(Pair.Value)) { SliderReferences.Emplace(Pair.Value); }
    }
    for (const auto& Pair : MuteSnapshot)
    {
        if (IsValid(Pair.Value)) { MuteReferences.Emplace(Pair.Value); }
    }

    // Preserve authored display callbacks; the native menu suppresses settings writes while synchronizing.
    if (MasterVolume.IsValid())
    {
        MasterVolume->SetValue(Snapshot.MasterVolume);
        MasterVolume->OnValueChanged.Broadcast(Snapshot.MasterVolume);
    }
    if (MasterMute.IsValid())
    {
        MasterMute->SetIsChecked(Snapshot.bMasterMuted);
        MasterMute->OnCheckStateChanged.Broadcast(Snapshot.bMasterMuted);
    }
    for (const auto& Pair : SliderSnapshot)
    {
        if (IsValid(Pair.Value) && IsSoundMenuChannel(Pair.Key))
        {
            const float Volume = Pair.Key == ESoundType::BGM ? Snapshot.BGMVolume : Snapshot.EffectVolume;
            Pair.Value->SetValue(Volume);
            Pair.Value->OnValueChanged.Broadcast(Volume);
        }
    }
    for (const auto& Pair : MuteSnapshot)
    {
        if (IsValid(Pair.Value) && IsSoundMenuChannel(Pair.Key))
        {
            const bool bMuted = Pair.Key == ESoundType::BGM ? Snapshot.bBGMMuted : Snapshot.bEffectMuted;
            Pair.Value->SetIsChecked(bMuted);
            Pair.Value->OnRPGMuteChanged.Broadcast(bMuted, Pair.Value);
        }
    }
}

bool IRPGSoundOptionsRuntime::ReadMenuSettings(const FRPGSoundOptionsControls& Controls,
    FRPGSoundSettings& OutSettings, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (!IsValid(Controls.MasterVolume) || !IsValid(Controls.MasterMute) || !IsValid(Controls.BGMVolume)
        || !IsValid(Controls.EffectVolume) || !IsValid(Controls.BGMMute) || !IsValid(Controls.EffectMute))
    {
        OutError = NSLOCTEXT("RPGSoundOptions", "MissingSaveControls", "사운드 설정을 불러오지 못했습니다.");
        return false;
    }
    FRPGSoundSettings Snapshot;
    Snapshot.MasterVolume = Controls.MasterVolume->GetValue();
    Snapshot.BGMVolume = Controls.BGMVolume->GetValue();
    Snapshot.EffectVolume = Controls.EffectVolume->GetValue();
    Snapshot.bMasterMuted = Controls.MasterMute->IsChecked();
    Snapshot.bBGMMuted = Controls.BGMMute->IsChecked();
    Snapshot.bEffectMuted = Controls.EffectMute->IsChecked();
    OutSettings = Snapshot;
    return true;
}

void IRPGSoundOptionsRuntime::InitializeMenu(const FRPGSoundOptionsControls& Controls,
    const TMap<ESoundType, USlider*>& Sliders, const TMap<ESoundType, URPGCheckBox*>& Mutes)
{
    const FRPGSoundSettings Snapshot = GetCurrentSettings();
    SynchronizeMenuControls(Snapshot, Controls, Sliders, Mutes);
}

void IRPGSoundOptionsRuntime::SaveMenu(const FRPGSoundOptionsControls& Controls)
{
    FText SaveError;
    if (!TrySaveMenu(Controls, SaveError))
    {
        UE_LOG(LogRPGSoundOptionsBridge, Warning, TEXT("Sound options save failed: %s"), *SaveError.ToString());
    }
}

bool IRPGSoundOptionsRuntime::TrySaveMenu(const FRPGSoundOptionsControls& Controls, FText& OutError)
{
    FRPGSoundSettings Requested;
    return ReadMenuSettings(Controls, Requested, OutError)
        && TrySaveSettings(Requested, Controls.MasterVolume, OutError);
}

void IRPGSoundOptionsRuntime::SetVolumeFromMenu(float Value, const TMap<ESoundType, USlider*>& Sliders)
{
    // A value-only compatibility callback must not guess when more than one control matches.
    USlider* MatchedSlider = nullptr;
    ESoundType MatchedType = ESoundType::Max;
    for (const auto& Pair : Sliders)
    {
        if (!IsSoundMenuChannel(Pair.Key) || !IsValid(Pair.Value) || Pair.Value->GetValue() != Value) { continue; }
        if (MatchedSlider) { return; }
        MatchedSlider = Pair.Value;
        MatchedType = Pair.Key;
    }
    if (MatchedSlider) { SetChannelVolume(MatchedType, Value, MatchedSlider); }
}

void IRPGSoundOptionsRuntime::SetMuteFromMenu(bool bMute, URPGCheckBox* CheckBox,
    const TMap<ESoundType, USlider*>& Sliders)
{
    if (!IsValid(CheckBox) || !IsSoundMenuChannel(CheckBox->GetSoundType())) { return; }
    const ESoundType Type = CheckBox->GetSoundType();
    USlider* const* Slider = Sliders.Find(Type);
    if (Slider && IsValid(*Slider)) { SetChannelMute(Type, bMute, (*Slider)->GetValue(), CheckBox); }
}
