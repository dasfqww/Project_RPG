#include "UI/Option/RPGGraphicsOptionsRuntime.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGGraphicsOptionsBridge, Log, All);

FName IRPGGraphicsOptionsRuntime::GetFeatureName()
{
    static const FName Name(TEXT("RPGGraphicsOptionsRuntime"));
    return Name;
}

IRPGGraphicsOptionsRuntime* IRPGGraphicsOptionsRuntime::Get()
{
    check(IsInGameThread());
    IModularFeatures& Features = IModularFeatures::Get();
    const FName Name = GetFeatureName();
    if (!Features.IsModularFeatureAvailable(Name))
    {
        // Preserve the existing Blueprint options APIs on maps without an Experience.
        // The optional module supplies stateless code; UI actions remain Experience-controlled.
        FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("RPGOptionsUIRuntime"));
    }
    if (Features.IsModularFeatureAvailable(Name))
    {
        return &Features.GetModularFeature<IRPGGraphicsOptionsRuntime>(Name);
    }
    UE_LOG(LogRPGGraphicsOptionsBridge, Error,
        TEXT("Graphics options implementation is unavailable. Enable the RPGOptionsUI plugin."));
    return nullptr;
}

namespace
{
    FString GetMenuWindowModeLabel(ERPGGraphicsWindowMode Mode)
    {
        switch (Mode)
        {
        case ERPGGraphicsWindowMode::Fullscreen: return TEXT("Fullscreen");
        case ERPGGraphicsWindowMode::Borderless: return TEXT("Windowed Fullscreen");
        default: return TEXT("Windowed");
        }
    }
}

void IRPGGraphicsOptionsRuntime::SynchronizeMenuControls(const FRPGGraphicsOptionsModel& Model,
    UComboBoxString* Resolution, UComboBoxString* WindowMode, UCheckBox* VSync)
{
    // Control callbacks can remove their owner. Retain only these controls for this synchronous update.
    TStrongObjectPtr<UComboBoxString> ResolutionControl(IsValid(Resolution) ? Resolution : nullptr);
    TStrongObjectPtr<UComboBoxString> WindowModeControl(IsValid(WindowMode) ? WindowMode : nullptr);
    TStrongObjectPtr<UCheckBox> VSyncControl(IsValid(VSync) ? VSync : nullptr);
    if (ResolutionControl.IsValid())
    {
        ResolutionControl->ClearOptions();
        for (const FIntPoint& Size : Model.Resolutions)
        {
            ResolutionControl->AddOption(FString::Printf(TEXT("%dx%d"), Size.X, Size.Y));
        }
        ResolutionControl->SetSelectedOption(FString::Printf(TEXT("%dx%d"),
            Model.Current.Resolution.X, Model.Current.Resolution.Y));
    }
    if (WindowModeControl.IsValid())
    {
        WindowModeControl->ClearOptions();
        for (ERPGGraphicsWindowMode Mode : Model.WindowModes)
        {
            WindowModeControl->AddOption(GetMenuWindowModeLabel(Mode));
        }
        WindowModeControl->SetSelectedOption(GetMenuWindowModeLabel(Model.Current.WindowMode));
    }
    if (VSyncControl.IsValid()) { VSyncControl->SetIsChecked(Model.Current.bVSync); }
}

bool IRPGGraphicsOptionsRuntime::ReadMenuSelection(UComboBoxString* Resolution, UComboBoxString* WindowMode,
    UCheckBox* VSync, FRPGGraphicsSettings& OutSettings, FText& OutError) const
{
    OutError = FText::GetEmpty();
    if (!IsValid(Resolution) || !IsValid(WindowMode) || !IsValid(VSync))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "MissingControls", "그래픽 설정을 불러오지 못했습니다.");
        return false;
    }
    const FString ResolutionValue = Resolution->GetSelectedOption();
    const FString WindowModeValue = WindowMode->GetSelectedOption();
    const bool bVSyncValue = VSync->IsChecked();
    return TryParseSelection(ResolutionValue, WindowModeValue, bVSyncValue, OutSettings, OutError);
}

void IRPGGraphicsOptionsRuntime::ConstructMenu(UComboBoxString* Resolution,
    UComboBoxString* WindowMode, UCheckBox* VSync)
{
    InitializeMenu(Resolution, WindowMode, VSync);
}

void IRPGGraphicsOptionsRuntime::InitializeMenu(UComboBoxString* Resolution,
    UComboBoxString* WindowMode, UCheckBox* VSync)
{
    FRPGGraphicsOptionsModel Model;
    BuildOptions(Model);
    SynchronizeMenuControls(Model, Resolution, WindowMode, VSync);
}

void IRPGGraphicsOptionsRuntime::ApplyMenu(UComboBoxString* Resolution,
    UComboBoxString* WindowMode, UCheckBox* VSync)
{
    FText Error;
    if (!TryApplyMenu(Resolution, WindowMode, VSync, Error))
    {
        UE_LOG(LogRPGGraphicsOptionsBridge, Warning, TEXT("Graphics options rejected: %s"), *Error.ToString());
    }
}

bool IRPGGraphicsOptionsRuntime::TryApplyMenu(UComboBoxString* Resolution,
    UComboBoxString* WindowMode, UCheckBox* VSync, FText& OutError)
{
    FRPGGraphicsSettings Requested;
    return ReadMenuSelection(Resolution, WindowMode, VSync, Requested, OutError)
        && TryApplySettings(Requested, OutError);
}

bool IRPGGraphicsOptionsRuntime::TryPreviewMenu(UComboBoxString* Resolution, UComboBoxString* WindowMode,
    UCheckBox* VSync, UObject* Owner, bool& bOutNeedsConfirmation, FText& OutError)
{
    bOutNeedsConfirmation = false;
    OutError = FText::GetEmpty();
    if (!IsValid(Owner))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "MissingControls", "그래픽 설정을 불러오지 못했습니다.");
        return false;
    }
    FRPGGraphicsSettings Requested;
    return ReadMenuSelection(Resolution, WindowMode, VSync, Requested, OutError)
        && TryPreviewSettings(Requested, Owner, bOutNeedsConfirmation, OutError);
}
