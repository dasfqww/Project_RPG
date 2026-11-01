#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Features/IModularFeature.h"

class APlayerController;
class UGameViewportClient;
class ULocalPlayer;
class URPGOptionMenu;
class UWorld;
class SWidget;
struct FKeyEvent;

/** UI navigation policy; it never changes authored gameplay mappings. */
enum class ERPGOptionsNavigationCommand : uint8
{
    None,
    Back,
    NextTab,
    PreviousTab
};

/** Shared viewport settings are retained by the participating options widgets. */
struct FRPGOptionsViewportInputState
{
    EMouseCaptureMode CaptureMode = EMouseCaptureMode::NoCapture;
    EMouseLockMode LockMode = EMouseLockMode::DoNotLock;
    bool bIgnoreInput = false;
    bool bHideCursorDuringCapture = false;
    TArray<TWeakObjectPtr<URPGOptionMenu>> Widgets;
};

/** Native value state; it holds no implementation pointer or rooted objects. */
struct FRPGOptionsMenuInputState
{
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<ULocalPlayer> LocalPlayer;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<UWorld> World;
    TWeakPtr<SWidget> Root;
    TWeakPtr<SWidget> AppliedFocus;
    TWeakPtr<SWidget> PreviousFocus;
    TWeakPtr<SWidget> PreviousActualFocus;
    TSharedPtr<FRPGOptionsViewportInputState> SharedViewport;
    FString PreviousDebugName;
    FGuid SessionId;
    int32 SlateUserIndex = INDEX_NONE;
    bool bPreviousCursorVisible = false;
    bool bPreviousFlushKeys = true;
    bool bPreviousFocusable = false;
    bool bActive = false;
    bool bApplied = false;
};

/** Options input and focus implementation supplied by RPGOptionsUIRuntime. */
class PROJECT_RPG_API IRPGOptionsMenuRuntime : public IModularFeature
{
public:
    static FName GetFeatureName();
    /** Acquire may load code for legacy maps; release passes false to avoid loading during teardown. */
    static IRPGOptionsMenuRuntime* Get(bool bLoadIfNeeded = true);

    virtual ERPGOptionsNavigationCommand GetNavigationCommand(const FKeyEvent& Event) const = 0;
    /** True only for the live player/world/session that still owns options focus and input. */
    virtual bool CanNavigate(const URPGOptionMenu* Widget, const FRPGOptionsMenuInputState& State) const = 0;
    virtual bool AcquireInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State) = 0;
    virtual void ReleaseInput(URPGOptionMenu* Widget, FRPGOptionsMenuInputState& State,
        bool bRestoreInput = true) = 0;
};
