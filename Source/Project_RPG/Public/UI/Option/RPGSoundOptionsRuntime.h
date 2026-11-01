#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Type/RPGEnumTypes.h"
#include "Settings/RPGSoundSettings.h"

class USlider;
class UWorld;
class UCheckBox;
class URPGCheckBox;

/** Borrowed controls used only by the Source UI adapter; feature policies receive values. */
struct FRPGSoundOptionsControls
{
    USlider* MasterVolume = nullptr;
    UCheckBox* MasterMute = nullptr;
    USlider* BGMVolume = nullptr;
    USlider* EffectVolume = nullptr;
    URPGCheckBox* BGMMute = nullptr;
    URPGCheckBox* EffectMute = nullptr;
};

class PROJECT_RPG_API IRPGSoundOptionsRuntime : public IModularFeature
{
public:
    static FName GetFeatureName();
    /** Game-thread lookup. Legacy callers may load the module without activating UI actions. */
    static IRPGSoundOptionsRuntime* Get();

    virtual FRPGSoundSettings GetCurrentSettings() const = 0;
    /** Apply one complete value snapshot without requesting persistence. */
    virtual bool TryApplySettings(const FRPGSoundSettings& Requested, const UObject* Context, FText& OutError) = 0;
    /** Apply, write JSON, then request INI saving. False also reports changed owners/values after a write.
     *  INI saving has no success return value; true does not guarantee INI disk durability. */
    virtual bool TrySaveSettings(const FRPGSoundSettings& Requested, const UObject* Context, FText& OutError) = 0;

    /** Source-owned UI adapters; no controls cross into feature policy calls. */
    static void SynchronizeMenuControls(const FRPGSoundSettings& Settings, const FRPGSoundOptionsControls& Controls,
        const TMap<ESoundType, USlider*>& Sliders, const TMap<ESoundType, URPGCheckBox*>& Mutes);
    static bool ReadMenuSettings(const FRPGSoundOptionsControls& Controls, FRPGSoundSettings& OutSettings, FText& OutError);

    /** Compatibility entry points implemented by the Source adapter. */
    virtual void InitializeMenu(const FRPGSoundOptionsControls& Controls,
        const TMap<ESoundType, USlider*>& Sliders,
        const TMap<ESoundType, URPGCheckBox*>& Mutes);
    virtual void SaveMenu(const FRPGSoundOptionsControls& Controls);
    virtual bool TrySaveMenu(const FRPGSoundOptionsControls& Controls, FText& OutError);
    virtual void SetMasterVolume(float Value, const UObject* Context = nullptr) = 0;
    virtual void SetMasterMute(bool bMute, float CurrentVolume, const UObject* Context = nullptr) = 0;
    /** Native menu callbacks identify their channel explicitly. */
    virtual void SetChannelVolume(ESoundType Type, float Value, const UObject* Context = nullptr) = 0;
    virtual void SetChannelMute(ESoundType Type, bool bMute, float CurrentVolume,
        const UObject* Context = nullptr) = 0;
    virtual void RestoreDefaultSettings(const UObject* Context = nullptr) = 0;
    virtual void SetVolumeFromMenu(float Value, const TMap<ESoundType, USlider*>& Sliders);
    virtual void SetMuteFromMenu(bool bMute, URPGCheckBox* CheckBox,
        const TMap<ESoundType, USlider*>& Sliders);
    /** Idempotent startup for a live client world. False means the owner is not ready yet. */
    virtual bool InitializeForWorld(UWorld* World) = 0;
    /** Explicit reload; normal startup callers should use InitializeForWorld. */
    virtual void RestoreSavedSettings(const UObject* Context = nullptr) = 0;
};
