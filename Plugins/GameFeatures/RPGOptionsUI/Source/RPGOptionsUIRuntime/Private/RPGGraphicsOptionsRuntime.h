#pragma once

#include "UI/Option/RPGGraphicsOptionsRuntime.h"

class UGameUserSettings;

/** No retained worlds, widgets or settings; each request resolves its existing owner. */
class FRPGGraphicsOptionsRuntime final : public IRPGGraphicsOptionsRuntime
{
public:
    virtual void BuildOptions(FRPGGraphicsOptionsModel& OutModel) const override;
    virtual bool TryParseSelection(const FString& Resolution, const FString& WindowMode, bool bVSync,
        FRPGGraphicsSettings& OutSettings, FText& OutError) const override;
    virtual bool TryApplySettings(const FRPGGraphicsSettings& Requested, FText& OutError) override;
    virtual bool TryPreviewSettings(const FRPGGraphicsSettings& Requested, UObject* Owner,
        bool& bOutNeedsConfirmation, FText& OutError) override;
    virtual bool TryConfirmPreview(const UObject* Owner, FText& OutError) override;
    virtual bool InitializeForWorld(UWorld* World) override;
    virtual void RestoreSavedSettings() override;

private:
    static bool ApplySettings(UGameUserSettings* Settings, const FRPGGraphicsSettings& Data,
        FText& OutError, bool bStartup = false);
    bool bGraphicsRequestActive = false;
};
