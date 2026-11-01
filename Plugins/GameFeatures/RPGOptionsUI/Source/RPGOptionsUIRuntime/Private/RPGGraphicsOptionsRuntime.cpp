#include "RPGGraphicsOptionsRuntime.h"

#include "ComponentRecreateRenderStateContext.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Settings/RPGGameUserSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "RPGOptionsPersistence.h"
#include "Type/RPGStructTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGGraphicsOptions, Log, All);

namespace
{
    bool ParsePositiveDimension(const FString& Text, int32& OutDimension)
    {
        const FString Digits = Text.TrimStartAndEnd();
        if (Digits.IsEmpty()) { return false; }
        int32 Dimension = 0;
        for (TCHAR Character : Digits)
        {
            if (Character < TEXT('0') || Character > TEXT('9')) { return false; }
            const int32 Digit = Character - TEXT('0');
            if (Dimension > (MAX_int32 - Digit) / 10) { return false; }
            Dimension = Dimension * 10 + Digit;
        }
        if (Dimension <= 0) { return false; }
        OutDimension = Dimension;
        return true;
    }

    bool ParseResolution(const FString& Text, FIntPoint& OutSize)
    {
        FString Width;
        FString Height;
        return Text.Split(TEXT("x"), &Width, &Height, ESearchCase::IgnoreCase)
            && ParsePositiveDimension(Width, OutSize.X) && ParsePositiveDimension(Height, OutSize.Y);
    }

    bool ParseWindowMode(const FString& Text, EWindowMode::Type& OutMode)
    {
        const FString Name = Text.TrimStartAndEnd();
        if (Name.Equals(TEXT("Fullscreen"), ESearchCase::IgnoreCase)) { OutMode = EWindowMode::Fullscreen; }
        else if (Name.Equals(TEXT("WindowedFullscreen"), ESearchCase::IgnoreCase)
            || Name.Equals(TEXT("Windowed Fullscreen"), ESearchCase::IgnoreCase))
        {
            OutMode = EWindowMode::WindowedFullscreen;
        }
        else if (Name.Equals(TEXT("Windowed"), ESearchCase::IgnoreCase)) { OutMode = EWindowMode::Windowed; }
        else { return false; }
        return true;
    }

    FString FormatResolution(FIntPoint Size)
    {
        return FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);
    }

    FString FormatWindowMode(EWindowMode::Type Mode)
    {
        if (Mode == EWindowMode::Fullscreen) { return TEXT("Fullscreen"); }
        if (Mode == EWindowMode::WindowedFullscreen) { return TEXT("WindowedFullscreen"); }
        return TEXT("Windowed");
    }

    ERPGGraphicsWindowMode FromEngineWindowMode(EWindowMode::Type Mode)
    {
        if (Mode == EWindowMode::Fullscreen) { return ERPGGraphicsWindowMode::Fullscreen; }
        if (Mode == EWindowMode::WindowedFullscreen) { return ERPGGraphicsWindowMode::Borderless; }
        return ERPGGraphicsWindowMode::Windowed;
    }

    bool ToEngineWindowMode(ERPGGraphicsWindowMode Mode, EWindowMode::Type& OutMode)
    {
        switch (Mode)
        {
        case ERPGGraphicsWindowMode::Fullscreen: OutMode = EWindowMode::Fullscreen; return true;
        case ERPGGraphicsWindowMode::Borderless: OutMode = EWindowMode::WindowedFullscreen; return true;
        case ERPGGraphicsWindowMode::Windowed: OutMode = EWindowMode::Windowed; return true;
        default: return false;
        }
    }

    bool ValidateSettings(const FRPGGraphicsSettings& Data, EWindowMode::Type& OutMode, FText& OutError)
    {
        if (Data.Resolution.X <= 0 || Data.Resolution.Y <= 0)
        {
            OutError = NSLOCTEXT("RPGGraphicsOptions", "InvalidResolution", "올바른 해상도를 선택해 주세요.");
            return false;
        }
        if (!ToEngineWindowMode(Data.WindowMode, OutMode))
        {
            OutError = NSLOCTEXT("RPGGraphicsOptions", "InvalidWindowMode", "화면 모드를 선택해 주세요.");
            return false;
        }
        return true;
    }

    FGraphicSaveData MakeSaveData(const FRPGGraphicsSettings& Settings)
    {
        FGraphicSaveData Data;
        EWindowMode::Type Mode = EWindowMode::Windowed;
        ToEngineWindowMode(Settings.WindowMode, Mode);
        Data.Resolution = FormatResolution(Settings.Resolution);
        Data.WindowMode = FormatWindowMode(Mode);
        Data.bVSync = Settings.bVSync;
        return Data;
    }

    bool IsCurrentSettings(const UGameUserSettings* Settings)
    {
        return IsValid(Settings) && !Settings->IsUnreachable()
            && !Settings->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
            && GEngine && GEngine->GetGameUserSettings() == Settings;
    }

    FText GraphicsRequestBusy()
    {
        return NSLOCTEXT("RPGGraphicsOptions", "RequestBusy", "그래픽 설정을 변경 중입니다. 잠시 후 다시 시도해 주세요.");
    }

    FText GraphicsSettingsChanged()
    {
        return NSLOCTEXT("RPGGraphicsOptions", "SettingsChanged", "설정이 변경되었습니다. 현재 값을 확인한 뒤 다시 시도해 주세요.");
    }

    FRPGGraphicsSettings ReadCurrentSettings(const UGameUserSettings* Settings = nullptr)
    {
        FRPGGraphicsSettings Data;
        if (!Settings) { Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr; }
        if (Settings)
        {
            FIntPoint Size = Settings->GetScreenResolution();
            if (Size.X <= 0 || Size.Y <= 0) { Size = Settings->GetDesktopResolution(); }
            if (Size.X > 0 && Size.Y > 0) { Data.Resolution = Size; }
            Data.WindowMode = FromEngineWindowMode(Settings->GetFullscreenMode());
            Data.bVSync = Settings->IsVSyncEnabled();
        }
        else
        {
            FGraphicSaveData SavedData;
            FIntPoint SavedSize = FIntPoint::ZeroValue;
            EWindowMode::Type SavedMode = EWindowMode::Windowed;
            if (RPGOptionsPersistence::LoadGraphics(SavedData)
                && ParseResolution(SavedData.Resolution, SavedSize) && ParseWindowMode(SavedData.WindowMode, SavedMode))
            {
                Data.Resolution = SavedSize;
                Data.WindowMode = FromEngineWindowMode(SavedMode);
                Data.bVSync = SavedData.bVSync;
            }
        }
        return Data;
    }

    bool MatchesCurrentSettings(const UGameUserSettings* Settings, const FRPGGraphicsSettings& Snapshot)
    {
        if (!IsCurrentSettings(Settings)) { return false; }
        const URPGGameUserSettings* RPGSettings = Cast<URPGGameUserSettings>(Settings);
        if (RPGSettings && RPGSettings->HasGraphicsPreview()) { return false; }
        const FRPGGraphicsSettings Current = ReadCurrentSettings(Settings);
        return Current.Resolution == Snapshot.Resolution && Current.WindowMode == Snapshot.WindowMode
            && Current.bVSync == Snapshot.bVSync;
    }

    bool PersistGraphicsSettings(UGameUserSettings* Settings, const FText& FailureMessage, FText& OutError)
    {
        if (!IsCurrentSettings(Settings))
        {
            OutError = GraphicsSettingsChanged();
            return false;
        }
        const FRPGGraphicsSettings Snapshot = ReadCurrentSettings(Settings);
        if (!MatchesCurrentSettings(Settings, Snapshot))
        {
            OutError = GraphicsSettingsChanged();
            return false;
        }
        if (!RPGOptionsPersistence::SaveGraphics(MakeSaveData(Snapshot), OutError))
        {
            OutError = FailureMessage;
            return false;
        }
        if (!MatchesCurrentSettings(Settings, Snapshot))
        {
            OutError = GraphicsSettingsChanged();
            return false;
        }
        Settings->SaveSettings();
        if (!MatchesCurrentSettings(Settings, Snapshot))
        {
            OutError = GraphicsSettingsChanged();
            return false;
        }
        return true;
    }
}

void FRPGGraphicsOptionsRuntime::BuildOptions(FRPGGraphicsOptionsModel& OutModel) const
{
    FRPGGraphicsOptionsModel Model;
    Model.Current = ReadCurrentSettings();
    TArray<FIntPoint> SupportedSizes;
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(SupportedSizes);
    for (const FIntPoint& Size : SupportedSizes)
    {
        if (Size.X > 0 && Size.Y > 0) { Model.Resolutions.AddUnique(Size); }
    }
    Model.Resolutions.AddUnique(Model.Current.Resolution);
    if (Model.Resolutions.IsEmpty()) { Model.Resolutions.Add(FIntPoint(1280, 720)); }
    Model.Resolutions.Sort([](const FIntPoint& Left, const FIntPoint& Right)
    {
        const int64 LeftArea = int64(Left.X) * Left.Y;
        const int64 RightArea = int64(Right.X) * Right.Y;
        if (LeftArea != RightArea) { return LeftArea < RightArea; }
        return Left.X != Right.X ? Left.X < Right.X : Left.Y < Right.Y;
    });
    Model.WindowModes = { ERPGGraphicsWindowMode::Fullscreen,
        ERPGGraphicsWindowMode::Borderless, ERPGGraphicsWindowMode::Windowed };
    OutModel = MoveTemp(Model);
}

bool FRPGGraphicsOptionsRuntime::TryParseSelection(const FString& Resolution, const FString& WindowMode,
    bool bVSync, FRPGGraphicsSettings& OutSettings, FText& OutError) const
{
    OutError = FText::GetEmpty();
    FRPGGraphicsSettings Parsed;
    EWindowMode::Type ParsedMode = EWindowMode::Windowed;
    if (!ParseResolution(Resolution, Parsed.Resolution))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "InvalidResolution", "올바른 해상도를 선택해 주세요.");
        return false;
    }
    if (!ParseWindowMode(WindowMode, ParsedMode))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "InvalidWindowMode", "화면 모드를 선택해 주세요.");
        return false;
    }
    Parsed.WindowMode = FromEngineWindowMode(ParsedMode);
    Parsed.bVSync = bVSync;
    OutSettings = Parsed;
    return true;
}

bool FRPGGraphicsOptionsRuntime::TryApplySettings(const FRPGGraphicsSettings& Requested, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (bGraphicsRequestActive) { OutError = GraphicsRequestBusy(); return false; }
    TGuardValue<bool> RequestGuard(bGraphicsRequestActive, true);
    const FRPGGraphicsSettings Snapshot(Requested);
    TStrongObjectPtr<UGameUserSettings> Settings(GEngine ? GEngine->GetGameUserSettings() : nullptr);
    if (!ApplySettings(Settings.Get(), Snapshot, OutError)) { return false; }
    if (!PersistGraphicsSettings(Settings.Get(), NSLOCTEXT("RPGGraphicsOptions", "AppliedNotSaved",
        "화면 설정은 적용했지만 저장하지 못했습니다. 다시 시도해 주세요."), OutError)) { return false; }
    UE_LOG(LogRPGGraphicsOptions, Log, TEXT("Graphics options applied by RPGOptionsUIRuntime."));
    return true;
}

bool FRPGGraphicsOptionsRuntime::TryPreviewSettings(const FRPGGraphicsSettings& Requested,
    UObject* Owner, bool& bOutNeedsConfirmation, FText& OutError)
{
    bOutNeedsConfirmation = false;
    OutError = FText::GetEmpty();
    if (bGraphicsRequestActive) { OutError = GraphicsRequestBusy(); return false; }
    const FRPGGraphicsSettings Snapshot(Requested);
    if (!IsValid(Owner))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "MissingControls", "그래픽 설정을 불러오지 못했습니다.");
        return false;
    }
    EWindowMode::Type RequestedMode = EWindowMode::Windowed;
    if (!ValidateSettings(Snapshot, RequestedMode, OutError)) { return false; }
    TStrongObjectPtr<UObject> PreviewOwner(Owner);
    TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
    if (!Settings.IsValid())
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "EngineUnavailable", "그래픽 설정이 아직 준비되지 않았습니다.");
        return false;
    }
    if (Settings->HasGraphicsPreview())
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "PreviewAlreadyActive", "진행 중인 화면 변경을 먼저 유지하거나 되돌려 주세요.");
        return false;
    }
    if (Settings->GetScreenResolution() == Snapshot.Resolution && Settings->GetFullscreenMode() == RequestedMode)
    {
        return TryApplySettings(Snapshot, OutError);
    }
    TGuardValue<bool> RequestGuard(bGraphicsRequestActive, true);
    if (!Settings->StartGraphicsPreview(PreviewOwner.Get(), Snapshot.Resolution, RequestedMode, Snapshot.bVSync))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "PreviewUnavailable", "화면 변경을 시작하지 못했거나 변경이 취소되었습니다.");
        return false;
    }
    if (!IsCurrentSettings(Settings.Get()) || !Settings->IsGraphicsPreviewOwner(PreviewOwner.Get()))
    {
        Settings->CancelGraphicsPreview(PreviewOwner.Get());
        OutError = GraphicsSettingsChanged();
        return false;
    }
    bOutNeedsConfirmation = true;
    return true;
}

bool FRPGGraphicsOptionsRuntime::TryConfirmPreview(const UObject* Owner, FText& OutError)
{
    OutError = FText::GetEmpty();
    if (bGraphicsRequestActive) { OutError = GraphicsRequestBusy(); return false; }
    TGuardValue<bool> RequestGuard(bGraphicsRequestActive, true);
    TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
    if (!Settings.IsValid() || !Settings->ConfirmGraphicsPreview(Owner))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "PreviewExpired", "확인 시간이 지났거나 화면 변경이 종료되었습니다.");
        return false;
    }
    return PersistGraphicsSettings(Settings.Get(), NSLOCTEXT("RPGGraphicsOptions", "ConfirmedNotSaved",
        "화면 설정은 유지했지만 저장하지 못했습니다. 적용 버튼으로 다시 저장해 주세요."), OutError);
}

bool FRPGGraphicsOptionsRuntime::InitializeForWorld(UWorld* World)
{
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
        || World->GetNetMode() == NM_DedicatedServer) { return false; }
    UGameInstance* Instance = World->GetGameInstance();
    if (!IsValid(Instance) || Instance->IsDedicatedServerInstance()) { return false; }

    // PIE shares the editor process and its display. Restore automatically in game/standalone worlds.
    if (World->WorldType == EWorldType::PIE) { return true; }
    TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
    if (!Settings.IsValid()) { return false; }
    if (Settings->bHasInitializedGraphicsOptions) { return true; }
    if (bGraphicsRequestActive) { return false; }
    TGuardValue<bool> RequestGuard(bGraphicsRequestActive, true);

    // Claim before engine callbacks. Missing or invalid JSON keeps the engine/INI settings for this session.
    Settings->bHasInitializedGraphicsOptions = true;
    FGraphicSaveData Data;
    if (RPGOptionsPersistence::LoadGraphics(Data))
    {
        FText ApplyError;
        FRPGGraphicsSettings Requested;
        if (!TryParseSelection(Data.Resolution, Data.WindowMode, Data.bVSync, Requested, ApplyError)
            || !ApplySettings(Settings.Get(), Requested, ApplyError, true))
        {
            UE_LOG(LogRPGGraphicsOptions, Warning, TEXT("Startup graphics options rejected: %s"), *ApplyError.ToString());
        }
    }
    return true;
}

void FRPGGraphicsOptionsRuntime::RestoreSavedSettings()
{
    if (bGraphicsRequestActive)
    {
        UE_LOG(LogRPGGraphicsOptions, Warning, TEXT("Saved graphics options rejected: %s"), *GraphicsRequestBusy().ToString());
        return;
    }
    TGuardValue<bool> RequestGuard(bGraphicsRequestActive, true);
    FGraphicSaveData Data;
    if (RPGOptionsPersistence::LoadGraphics(Data))
    {
        TStrongObjectPtr<UGameUserSettings> Settings(GEngine ? GEngine->GetGameUserSettings() : nullptr);
        FText ApplyError;
        FRPGGraphicsSettings Requested;
        if (!TryParseSelection(Data.Resolution, Data.WindowMode, Data.bVSync, Requested, ApplyError)
            || !ApplySettings(Settings.Get(), Requested, ApplyError))
        {
            UE_LOG(LogRPGGraphicsOptions, Warning, TEXT("Saved graphics options rejected: %s"), *ApplyError.ToString());
            return;
        }
        const FRPGGraphicsSettings Snapshot = ReadCurrentSettings(Settings.Get());
        Settings->SaveSettings();
        if (!MatchesCurrentSettings(Settings.Get(), Snapshot))
        {
            UE_LOG(LogRPGGraphicsOptions, Warning, TEXT("Saved graphics settings changed during INI save."));
        }
    }
}

bool FRPGGraphicsOptionsRuntime::ApplySettings(UGameUserSettings* Settings,
    const FRPGGraphicsSettings& Data, FText& OutError, bool bStartup)
{
    OutError = FText::GetEmpty();
    const FRPGGraphicsSettings Snapshot(Data);
    EWindowMode::Type RequestedMode = EWindowMode::Windowed;
    if (!ValidateSettings(Snapshot, RequestedMode, OutError)) { return false; }
    if (!IsInGameThread() || !IsCurrentSettings(Settings))
    {
        OutError = NSLOCTEXT("RPGGraphicsOptions", "EngineUnavailable", "그래픽 설정이 아직 준비되지 않았습니다.");
        return false;
    }
    TStrongObjectPtr<UGameUserSettings> KeepSettingsAlive(Settings);
    if (URPGGameUserSettings* RPGSettings = Cast<URPGGameUserSettings>(Settings))
    {
        if (RPGSettings->HasGraphicsPreview())
        {
            OutError = NSLOCTEXT("RPGGraphicsOptions", "PreviewAlreadyActive", "진행 중인 화면 변경을 먼저 유지하거나 되돌려 주세요.");
            return false;
        }
        if (!RPGSettings->ApplyGraphicsOptionsWithoutSaving(
            Snapshot.Resolution, RequestedMode, Snapshot.bVSync, bStartup))
        {
            OutError = GraphicsSettingsChanged();
            return false;
        }
    }
    else
    {
        // Compatibility for a base engine settings object; the project settings path also masks nested saves.
        Settings->SetScreenResolution(Snapshot.Resolution);
        Settings->SetFullscreenMode(RequestedMode);
        Settings->SetVSyncEnabled(Snapshot.bVSync);
        {
            FGlobalComponentRecreateRenderStateContext RenderStateContext;
            Settings->ApplyResolutionSettings(bStartup);
            if (!IsCurrentSettings(Settings)) { OutError = GraphicsSettingsChanged(); return false; }
            Settings->ApplyNonResolutionSettings();
        }
        if (!IsCurrentSettings(Settings)) { OutError = GraphicsSettingsChanged(); return false; }
        if (!bStartup) { Settings->RequestUIUpdate(); }
    }
    if (!IsCurrentSettings(Settings)) { OutError = GraphicsSettingsChanged(); return false; }
    return true;
}
