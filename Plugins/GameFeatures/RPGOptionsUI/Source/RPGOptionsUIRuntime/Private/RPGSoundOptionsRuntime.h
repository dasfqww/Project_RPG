#pragma once

#include "UI/Option/RPGSoundOptionsRuntime.h"
#include "Templates/Function.h"

/** Settings policy only; sound playback and sound-mix ownership stay in the existing manager. */
class FRPGSoundOptionsRuntime final : public IRPGSoundOptionsRuntime
{
public:
    virtual FRPGSoundSettings GetCurrentSettings() const override;
    virtual bool TryApplySettings(const FRPGSoundSettings& Requested, const UObject* Context, FText& OutError) override;
    virtual bool TrySaveSettings(const FRPGSoundSettings& Requested, const UObject* Context, FText& OutError) override;
    virtual void SetMasterVolume(float Value, const UObject* Context = nullptr) override;
    virtual void SetMasterMute(bool bMute, float CurrentVolume, const UObject* Context = nullptr) override;
    virtual void SetChannelVolume(ESoundType Type, float Value, const UObject* Context = nullptr) override;
    virtual void SetChannelMute(ESoundType Type, bool bMute, float CurrentVolume,
        const UObject* Context = nullptr) override;
    virtual void RestoreDefaultSettings(const UObject* Context = nullptr) override;
    virtual bool InitializeForWorld(UWorld* World) override;
    virtual void RestoreSavedSettings(const UObject* Context = nullptr) override;

private:
    void UpdateSettings(const UObject* Context, TFunctionRef<void(FRPGSoundSettings&)> Update);
    bool bSoundRequestActive = false;
};
