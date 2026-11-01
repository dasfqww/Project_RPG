#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

class UWorld;
class UCheckBox;
class UComboBoxString;

/** Stable C++ values; the feature translates these to engine display modes. */
enum class ERPGGraphicsWindowMode : uint8
{
    Fullscreen,
    Borderless,
    Windowed
};

struct FRPGGraphicsSettings
{
    FIntPoint Resolution = FIntPoint(1280, 720);
    ERPGGraphicsWindowMode WindowMode = ERPGGraphicsWindowMode::Windowed;
    bool bVSync = false;
};

/** Choices and current settings are owned values, independent of any widget lifetime. */
struct FRPGGraphicsOptionsModel
{
    FRPGGraphicsSettings Current;
    TArray<FIntPoint> Resolutions;
    TArray<ERPGGraphicsWindowMode> WindowModes;
};

/** Stateless graphics-settings implementation supplied by the options GameFeature module. */
class PROJECT_RPG_API IRPGGraphicsOptionsRuntime : public IModularFeature
{
public:
    static FName GetFeatureName();

    /**
     * Resolve the registered implementation on the game thread.
     * Legacy options callers may load the implementation module without activating its UI actions.
     * Do not retain this pointer across calls or module shutdown.
     */
    static IRPGGraphicsOptionsRuntime* Get();

    virtual void BuildOptions(FRPGGraphicsOptionsModel& OutModel) const = 0;
    /** Text parsing is retained for authored menus and saved-data compatibility. No controls are retained. */
    virtual bool TryParseSelection(const FString& Resolution, const FString& WindowMode, bool bVSync,
        FRPGGraphicsSettings& OutSettings, FText& OutError) const = 0;
    virtual bool TryApplySettings(const FRPGGraphicsSettings& Requested, FText& OutError) = 0;
    virtual bool TryPreviewSettings(const FRPGGraphicsSettings& Requested, UObject* Owner,
        bool& bOutNeedsConfirmation, FText& OutError) = 0;

    /** Source-owned UI adapters. The feature only receives the model or a copied settings selection. */
    static void SynchronizeMenuControls(const FRPGGraphicsOptionsModel& Model,
        UComboBoxString* Resolution, UComboBoxString* WindowMode, UCheckBox* VSync);
    bool ReadMenuSelection(UComboBoxString* Resolution, UComboBoxString* WindowMode, UCheckBox* VSync,
        FRPGGraphicsSettings& OutSettings, FText& OutError) const;

    /** Compatibility entry points for existing C++ callers. Implemented by the Source UI adapter. */
    virtual void ConstructMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
        UCheckBox* VSync);
    virtual void InitializeMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
        UCheckBox* VSync);
    virtual void ApplyMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
        UCheckBox* VSync);
    /** Reports validation and JSON persistence failures. A save failure may follow successful application. */
    virtual bool TryApplyMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
        UCheckBox* VSync, FText& OutError);
    /** Display changes are provisional; VSync-only changes use the immediate apply/save path. */
    virtual bool TryPreviewMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
        UCheckBox* VSync, UObject* Owner, bool& bOutNeedsConfirmation, FText& OutError);
    virtual bool TryConfirmPreview(const UObject* Owner, FText& OutError) = 0;
    /** Idempotent startup for a live client world. False means the owner is not ready yet. */
    virtual bool InitializeForWorld(UWorld* World) = 0;
    /** Explicit reload; normal startup callers should use InitializeForWorld. */
    virtual void RestoreSavedSettings() = 0;
};
