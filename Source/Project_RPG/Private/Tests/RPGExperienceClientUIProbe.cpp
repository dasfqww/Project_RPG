// Opt-in proof of the real development map's client UI; excluded from Shipping.
#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/InputDeviceLibrary.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "LoadingScreenManager.h"
#include "LoadingProcessTask.h"
#include "Settings/RPGExperienceSettings.h"
#include "UI/Loading/RPGExperienceFailureNotice.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Text/STextBlock.h"
#include "Layout/Children.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Loading/RPGExperienceLoadingScreen.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"

namespace
{
struct FClientUIInputSnapshot
{
    bool Cursor = false;
    bool Ignore = false;
    bool HideDuringCapture = false;
    bool FlushOnFocus = false;
    EMouseCaptureMode Capture = EMouseCaptureMode::NoCapture;
    EMouseLockMode Lock = EMouseLockMode::DoNotLock;
    TWeakPtr<SWidget> Focus;

    void Read(APlayerController& PC, UGameViewportClient& Viewport, FSlateUser& User)
    {
        Cursor = PC.bShowMouseCursor;
        Ignore = Viewport.IgnoreInput();
        HideDuringCapture = Viewport.HideCursorDuringCapture();
        FlushOnFocus = PC.ShouldFlushKeysWhenViewportFocusChanges();
        Capture = Viewport.GetMouseCaptureMode();
        Lock = Viewport.GetMouseLockMode();
        Focus = User.GetFocusedWidget();
    }

    bool Equals(const FClientUIInputSnapshot& Other) const
    {
        return Cursor == Other.Cursor && Ignore == Other.Ignore
            && HideDuringCapture == Other.HideDuringCapture && FlushOnFocus == Other.FlushOnFocus
            && Capture == Other.Capture && Lock == Other.Lock && Focus.Pin() == Other.Focus.Pin();
    }

    TSharedRef<FJsonObject> Json() const
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("CursorVisible"), Cursor);
        Result->SetBoolField(TEXT("IgnoreInput"), Ignore);
        Result->SetBoolField(TEXT("HideCursorDuringCapture"), HideDuringCapture);
        Result->SetBoolField(TEXT("FlushInputOnViewportFocus"), FlushOnFocus);
        Result->SetNumberField(TEXT("MouseCaptureMode"), static_cast<int32>(Capture));
        Result->SetNumberField(TEXT("MouseLockMode"), static_cast<int32>(Lock));
        const TSharedPtr<SWidget> Widget = Focus.Pin();
        Result->SetStringField(TEXT("FocusType"), Widget ? Widget->GetTypeAsString() : TEXT("None"));
        return Result;
    }
};

struct FExperienceClientUIProof
{
    double Started = FPlatformTime::Seconds();
    double PhaseStarted = Started;
    int32 Phase = 0;
    int32 Cycle = 0;
    uint64 OpenedAtFrame = 0;
    bool KeyDown = false;
    bool SawLoading = false;
    bool SawLoadingWidget = false;
    bool Passed = false;
    FString Output;
    FString Failure;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<URPGOptionMenu> OriginalMenu;
    FClientUIInputSnapshot Baseline;
    TArray<TSharedPtr<FJsonValue>> Checks;
    TArray<FString> Screenshots;

    TSharedPtr<FJsonObject> MenuLayout;

    bool InspectMenu(URPGOptionMenu& Menu, UGameViewportClient& Viewport)
    {
        MenuLayout = MakeShared<FJsonObject>();
        MenuLayout->SetBoolField(TEXT("Activated"), Menu.IsActivated());
        TArray<TSharedPtr<FJsonValue>> Widgets;
        bool ContentOnScreen = false;
        const FSlateRect ViewRect = Viewport.GetGameViewportWidget()->GetCachedGeometry().GetRenderBoundingRect();
        for (TObjectIterator<UWidget> It; It; ++It)
        {
            if (!It->IsIn(&Menu)) { continue; }
            const auto Size = It->GetCachedGeometry().GetLocalSize();
            const auto Position = It->GetCachedGeometry().GetAbsolutePosition();
            const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("Name"), It->GetName());
            Item->SetStringField(TEXT("Class"), It->GetClass()->GetName());
            Item->SetNumberField(TEXT("Visibility"), static_cast<int32>(It->GetVisibility()));
            Item->SetNumberField(TEXT("Opacity"), It->GetRenderOpacity());
            Item->SetNumberField(TEXT("Width"), Size.X);
            Item->SetNumberField(TEXT("Height"), Size.Y);
            Item->SetNumberField(TEXT("X"), Position.X);
            Item->SetNumberField(TEXT("Y"), Position.Y);
            Widgets.Add(MakeShared<FJsonValueObject>(Item));
            if (It->GetName() == TEXT("TabSwitcher"))
            {
                const FSlateRect Rect = It->GetCachedGeometry().GetRenderBoundingRect();
                ContentOnScreen = It->IsVisible() && It->GetRenderOpacity() > 0.f && Size.X > 1.f && Size.Y > 1.f
                    && FMath::Min(Rect.Right, ViewRect.Right) > FMath::Max(Rect.Left, ViewRect.Left)
                    && FMath::Min(Rect.Bottom, ViewRect.Bottom) > FMath::Max(Rect.Top, ViewRect.Top);
            }
        }
        MenuLayout->SetArrayField(TEXT("Widgets"), Widgets);
        MenuLayout->SetBoolField(TEXT("ContentOnScreen"), ContentOnScreen);
        return ContentOnScreen;
    }

    void Checkpoint(const TCHAR* Name, const FClientUIInputSnapshot& Snapshot)
    {
        const TSharedRef<FJsonObject> Item = Snapshot.Json();
        Item->SetStringField(TEXT("Check"), Name);
        Item->SetNumberField(TEXT("Cycle"), Cycle);
        Item->SetBoolField(TEXT("Passed"), true);
        Checks.Add(MakeShared<FJsonValueObject>(Item));
        UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_UI_PROOF CHECK=%s Cycle=%d"), Name, Cycle);
    }

    void Screenshot(const TCHAR* Name)
    {
        const FString Path = FPaths::Combine(Output, FString(Name) + TEXT(".png"));
        Screenshots.Add(Path);
        FScreenshotRequest::RequestScreenshot(Path, true, false, false, FIntRect(), true);
    }

    bool Finish(const FString& Reason = FString())
    {
        Failure = Reason;
        Passed = Failure.IsEmpty();
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("Passed"), Passed);
        Report->SetStringField(TEXT("Failure"), Failure);
        Report->SetStringField(TEXT("Map"), World.IsValid() ? World->GetPackage()->GetName() : TEXT("None"));
        Report->SetStringField(TEXT("InputRoute"), TEXT("GameViewportClient InputKey -> EnhancedInput -> options shortcut"));
        Report->SetBoolField(TEXT("PhysicalKeyboardRoutingVerified"), false);
        Report->SetBoolField(TEXT("LoadingScreenObserved"), SawLoading);
        Report->SetBoolField(TEXT("NativeLoadingWidgetObserved"), SawLoadingWidget);
        Report->SetNumberField(TEXT("MenuFramesBeforeCapture"), 3);
        Report->SetNumberField(TEXT("CompletedCycles"), Cycle);
        Report->SetArrayField(TEXT("Checks"), Checks);
        if (MenuLayout) { Report->SetObjectField(TEXT("OptionsLayout"), MenuLayout); }
        TArray<TSharedPtr<FJsonValue>> Images;
        for (const FString& Path : Screenshots) { Images.Add(MakeShared<FJsonValueString>(Path)); }
        Report->SetArrayField(TEXT("Screenshots"), Images);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        if (!FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("Report.json"))))
        {
            Passed = false;
            UE_LOG(LogTemp, Error, TEXT("RPG_EXPERIENCE_UI_PROOF could not save report"));
        }
        if (Passed) { UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_UI_PROOF PASSED Cycles=%d"), Cycle); }
        else { UE_LOG(LogTemp, Error, TEXT("RPG_EXPERIENCE_UI_PROOF FAILED Reason=%s"), *Failure); }
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return false;
    }

    void SendKey(UGameViewportClient& Viewport, EInputEvent Event)
    {
        const FInputDeviceId Device = UInputDeviceLibrary::GetDefaultInputDevice();
        const FInputKeyEventArgs Args(Viewport.Viewport, Device, EKeys::F10, Event, FPlatformTime::Cycles64());
        Viewport.InputKey(Args);
        KeyDown = Event == IE_Pressed;
    }

    bool HasBindings(ARPGHUD& HUD) const
    {
        UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsMenuComponent"));
        UActorComponent* Component = Class ? HUD.FindComponentByClass(Class) : nullptr;
        UFunction* Function = Component ? Component->FindFunction(TEXT("HasShortcutBindings")) : nullptr;
        if (!Function) { return false; }
        TArray<uint8> Parameters;
        Parameters.SetNumZeroed(Function->ParmsSize);
        Component->ProcessEvent(Function, Parameters.GetData());
        const FBoolProperty* Result = CastField<FBoolProperty>(Function->GetReturnProperty());
        return Result && Result->GetPropertyValue_InContainer(Parameters.GetData());
    }

    bool Tick(float)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 60.0) { return Finish(TEXT("Timed out waiting for UI proof")); }
        if (!World.IsValid() && GEngine)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* Candidate = Context.World();
                if (Candidate && Candidate->IsGameWorld()
                    && Candidate->GetPackage()->GetName() == TEXT("/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment"))
                {
                    World = Candidate;
                    break;
                }
            }
        }
        UWorld* CurrentWorld = World.Get();
        if (!CurrentWorld) { return true; }
        if (CurrentWorld->bIsTearingDown) { return Finish(TEXT("World ended before verification completed")); }
        UGameViewportClient* Viewport = CurrentWorld->GetGameViewport();
        UGameInstance* Instance = CurrentWorld->GetGameInstance();
        AGameStateBase* State = CurrentWorld->GetGameState();
        URPGExperienceManagerComponent* Experience = State
            ? State->FindComponentByClass<URPGExperienceManagerComponent>() : nullptr;
        ULoadingScreenManager* Loading = Instance ? Instance->GetSubsystem<ULoadingScreenManager>() : nullptr;
        if (Experience && Experience->GetLoadState() == ERPGExperienceLoadState::Failed)
        {
            return Finish(TEXT("Experience failed: ") + Experience->GetLastError());
        }
        const bool LoadingVisible = Loading && Loading->GetLoadingScreenDisplayStatus();
        if (LoadingVisible && !SawLoading)
        {
            SawLoading = true;
            for (TObjectIterator<URPGExperienceLoadingScreen> It; It; ++It)
            {
                if (!It->HasAnyFlags(RF_ClassDefaultObject) && It->GetWorld() == CurrentWorld && It->GetCachedWidget())
                {
                    SawLoadingWidget = true;
                    break;
                }
            }
            Screenshot(TEXT("Loading"));
        }
        APlayerController* PC = Instance ? Instance->GetFirstLocalPlayerController() : nullptr;
        ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
        const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
        ARPGHUD* HUD = PC ? Cast<ARPGHUD>(PC->GetHUD()) : nullptr;
        URPGOptionMenu* Menu = HUD ? HUD->GetOptionMenuWidget() : nullptr;
        if (Phase == 0)
        {
            if (!Experience || !Experience->IsExperienceLoaded() || LoadingVisible
                || !Viewport || !PC || !Player || !User || !HUD || !Menu || !HasBindings(*HUD)) { return true; }
            if (!SawLoading || !SawLoadingWidget) { return Finish(TEXT("Native global loading screen was not observed")); }
            if (Experience->GetSelectedExperiencePath().ToString()
                != TEXT("/Game/System/Experiences/Development/DA_CommonClientDevelopment.DA_CommonClientDevelopment"))
            {
                return Finish(TEXT("Wrong Experience selected"));
            }
            int32 Hints = 0;
            for (TObjectIterator<UUserWidget> It; It; ++It)
            {
                if (It->GetOwningPlayer() == PC && It->IsInViewport()
                    && It->GetClass()->GetPathName() == TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint")) { ++Hints; }
            }
            if (Hints != 1 || Menu->IsVisible()) { return Finish(TEXT("Incorrect initial menu or shortcut hint count")); }
            OriginalMenu = Menu;
            PC->SetInputMode(FInputModeGameOnly());
            PC->SetShowMouseCursor(false);
            FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Viewport->GetGameViewportWidget(), EFocusCause::SetDirectly);
            Phase = 1;
            PhaseStarted = Now;
            return true;
        }
        if (!Viewport || !PC || !Player || !User || !HUD || Menu != OriginalMenu.Get())
        {
            return Finish(TEXT("Local UI ownership changed during verification"));
        }
        if (KeyDown && Now - PhaseStarted >= 0.15) { SendKey(*Viewport, IE_Released); }
        if (Phase == 1 && Now - PhaseStarted >= 0.4)
        {
            Baseline.Read(*PC, *Viewport, *User);
            if (Baseline.Focus.Pin() != Viewport->GetGameViewportWidget())
            {
                return Finish(TEXT("Could not establish viewport focus baseline"));
            }
            Checkpoint(TEXT("ReadyBaseline"), Baseline);
            Screenshot(TEXT("Ready"));
            SendKey(*Viewport, IE_Pressed);
            Phase = 2;
            PhaseStarted = Now;
        }
        else if (Phase == 2 && Now - PhaseStarted >= 0.4)
        {
            if (!Menu->IsVisible() || !HUD->bIsOptionMenuVisible)
            {
                if (Now - PhaseStarted > 5.0) { return Finish(TEXT("F10 did not open the options menu")); }
                return true;
            }
            if (OpenedAtFrame == 0) { OpenedAtFrame = GFrameCounter; return true; }
            if (GFrameCounter < OpenedAtFrame + 3) { return true; }
            FClientUIInputSnapshot Opened;
            Opened.Read(*PC, *Viewport, *User);
            if (!Opened.Cursor || Opened.Ignore || Opened.HideDuringCapture || Opened.FlushOnFocus
                || Opened.Capture != EMouseCaptureMode::CaptureDuringMouseDown
                || Opened.Lock != EMouseLockMode::DoNotLock
                || !User->IsWidgetInFocusPath(Menu->GetCachedWidget()))
            {
                return Finish(TEXT("Options input mode or focus was not applied"));
            }
            int32 Menus = 0;
            for (TObjectIterator<URPGOptionMenu> It; It; ++It)
            {
                if (It->GetOwningPlayer() == PC && It->IsInViewport()) { ++Menus; }
            }
            if (Menus != 1) { return Finish(TEXT("Duplicate options menu in viewport")); }
            if (!InspectMenu(*Menu, *Viewport))
            {
                if (Now - PhaseStarted < 5.0) { return true; }
                return Finish(TEXT("Options content has no visible layout inside the viewport"));
            }
            Checkpoint(TEXT("OpenedByF10"), Opened);
            if (Cycle == 0) { Screenshot(TEXT("OptionsOpen")); }
            Phase = 3;
            PhaseStarted = Now;
        }
        else if (Phase == 3 && Now - PhaseStarted >= 0.6)
        {
            if (Cycle == 0 && IFileManager::Get().FileSize(*Screenshots.Last()) <= 0)
            {
                if (Now - PhaseStarted > 5.0) { return Finish(TEXT("Options screenshot was not processed before closing")); }
                return true;
            }
            SendKey(*Viewport, IE_Pressed);
            Phase = 4;
            PhaseStarted = Now;
        }
        else if (Phase == 4 && Now - PhaseStarted >= 0.4)
        {
            if (Menu->IsVisible() || HUD->bIsOptionMenuVisible)
            {
                if (Now - PhaseStarted > 5.0) { return Finish(TEXT("F10 did not close the options menu")); }
                return true;
            }
            FClientUIInputSnapshot Closed;
            Closed.Read(*PC, *Viewport, *User);
            if (!Closed.Equals(Baseline)) { return Finish(TEXT("Closing options did not restore input, cursor and actual focus")); }
            Checkpoint(TEXT("ClosedAndRestored"), Closed);
            ++Cycle;
            if (Cycle < 2)
            {
                OpenedAtFrame = 0;
                SendKey(*Viewport, IE_Pressed);
                Phase = 2;
                PhaseStarted = Now;
            }
            else
            {
                Screenshot(TEXT("Closed"));
                Phase = 5;
                PhaseStarted = Now;
            }
        }
        else if (Phase == 5 && Now - PhaseStarted >= 1.0)
        {
            for (const FString& Path : Screenshots)
            {
                if (IFileManager::Get().FileSize(*Path) <= 0)
                {
                    if (Now - PhaseStarted > 5.0) { return Finish(TEXT("A requested UI screenshot was not saved")); }
                    return true;
                }
            }
            return Finish();
        }
        return true;
    }
};


struct FExperienceFailureUIProof : TSharedFromThis<FExperienceFailureUIProof>
{
    double Started = FPlatformTime::Seconds();
    double PhaseStarted = Started;
    int32 Phase = 0;
    uint64 VisibleFrame = 0;
    bool HoldOtherLoading = false;
    bool ExpectNoNotice = false;
    bool SawFailedLoading = false;
    bool HoldObserved = false;
    bool NoticeObserved = false;
    bool InputUnchanged = false;
    bool CleanupObserved = false;
    bool HookedEnd = false;
    bool Passed = false;
    FString Output;
    FString Failure;
    FString InternalReason;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<URPGExperienceManagerComponent> Manager;
    TStrongObjectPtr<ULoadingProcessTask> OtherLoading;
    FClientUIInputSnapshot Baseline;
    TArray<FString> Screenshots;

    static void ReadSlate(const TSharedRef<SWidget>& Widget, TArray<FString>& Texts, int32& VisibleSpinners, int32 Depth = 0)
    {
        if (Depth > 32) { return; }
        if (Widget->GetType() == FName(TEXT("STextBlock")))
        {
            Texts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
        }
        if (Widget->GetType() == FName(TEXT("SThrobber")) && Widget->GetVisibility().IsVisible())
        {
            ++VisibleSpinners;
        }
        FChildren* Children = Widget->GetChildren();
        for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
        {
            ReadSlate(Children->GetChildAt(Index), Texts, VisibleSpinners, Depth + 1);
        }
    }

    bool HasSafeFailureText(UUserWidget& Widget, bool CheckSpinner) const
    {
        const TSharedPtr<SWidget> Root = Widget.GetCachedWidget();
        if (!Root) { return false; }
        TArray<FString> Texts;
        int32 Spinners = 0;
        ReadSlate(Root.ToSharedRef(), Texts, Spinners);
        const FString Title = TEXT("\uAC8C\uC784\uC744 \uC2DC\uC791\uD560 \uC218 \uC5C6\uC2B5\uB2C8\uB2E4");
        const FString Message = GetDefault<URPGExperienceSettings>()->LoadingFailureMessage.ToString();
        if (!Texts.Contains(Title) || !Texts.Contains(Message) || (CheckSpinner && Spinners != 0)) { return false; }
        for (const FString& Text : Texts)
        {
            if (Text.Contains(TEXT("/Game/")) || Text.Contains(TEXT("DA_FailureUIAbsent"))
                || Text.Contains(TEXT("RPGExperienceDefinition:"))) { return false; }
        }
        return true;
    }

    int32 CountNotices(bool VisibleOnly = false) const
    {
        int32 Count = 0;
        for (TObjectIterator<URPGExperienceFailureNotice> It; It; ++It)
        {
            if (It->GetWorld() == World.Get() && It->IsInViewport() && (!VisibleOnly || It->IsVisible())) { ++Count; }
        }
        return Count;
    }

    URPGExperienceFailureNotice* FindNotice(APlayerController& PC) const
    {
        for (TObjectIterator<URPGExperienceFailureNotice> It; It; ++It)
        {
            if (It->GetWorld() == World.Get() && It->GetOwningPlayer() == &PC && It->IsInViewport()) { return *It; }
        }
        return nullptr;
    }

    void Screenshot(const TCHAR* Name)
    {
        const FString Path = FPaths::Combine(Output, FString(Name) + TEXT(".png"));
        Screenshots.Add(Path);
        FScreenshotRequest::RequestScreenshot(Path, true, false, false, FIntRect(), true);
    }

    bool ScreenshotsSaved() const
    {
        for (const FString& Path : Screenshots) { if (IFileManager::Get().FileSize(*Path) <= 0) { return false; } }
        return true;
    }

    bool SaveReport() const
    {
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("Passed"), Passed);
        Report->SetStringField(TEXT("Failure"), Failure);
        Report->SetStringField(TEXT("ExpectedInternalReason"), InternalReason);
        Report->SetBoolField(TEXT("FailedLoadingScreenObserved"), SawFailedLoading);
        Report->SetBoolField(TEXT("IndependentLoadingHoldVerified"), HoldOtherLoading && HoldObserved);
        Report->SetBoolField(TEXT("NoticeDisabled"), ExpectNoNotice);
        Report->SetBoolField(TEXT("PassiveNoticeObserved"), NoticeObserved);
        Report->SetBoolField(TEXT("InputAndActualFocusUnchanged"), InputUnchanged);
        Report->SetBoolField(TEXT("NoticeRemovedOnManagerEndPlay"), CleanupObserved);
        Report->SetObjectField(TEXT("InputBaseline"), Baseline.Json());
        TArray<TSharedPtr<FJsonValue>> Images;
        for (const FString& Path : Screenshots) { Images.Add(MakeShared<FJsonValueString>(Path)); }
        Report->SetArrayField(TEXT("Screenshots"), Images);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        return FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("Report.json")));
    }

    bool Finish(const FString& Reason = FString())
    {
        if (OtherLoading) { OtherLoading->Unregister(); OtherLoading.Reset(); }
        Failure = Reason;
        Passed = Failure.IsEmpty();
        if (!SaveReport()) { Passed = false; }
        if (Passed) { UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_FAILURE_UI_PROOF PASSED")); }
        else { UE_LOG(LogTemp, Error, TEXT("RPG_EXPERIENCE_FAILURE_UI_PROOF FAILED Reason=%s"), *Failure); }
        FPlatformMisc::RequestExitWithStatus(false, Passed ? 0 : 1);
        return false;
    }

    bool Tick(float)
    {
        const double Now = FPlatformTime::Seconds();
        if (Now - Started > 60.0) { return Finish(TEXT("Timed out waiting for failure UI")); }
        if (!World.IsValid() && GEngine)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* Candidate = Context.World();
                if (Candidate && Candidate->IsGameWorld()
                    && Candidate->GetPackage()->GetName() == TEXT("/Game/System/Experiences/Development/Maps/L_ExperienceDevelopment"))
                {
                    World = Candidate;
                    break;
                }
            }
        }
        UWorld* CurrentWorld = World.Get();
        if (!CurrentWorld || !CurrentWorld->GetGameInstance()) { return true; }
        if (CurrentWorld->bIsTearingDown) { return Finish(TEXT("World ended before failure UI verification")); }
        UGameInstance* Instance = CurrentWorld->GetGameInstance();
        AGameStateBase* State = CurrentWorld->GetGameState();
        URPGExperienceManagerComponent* Experience = State ? State->FindComponentByClass<URPGExperienceManagerComponent>() : nullptr;
        ULoadingScreenManager* Loading = Instance->GetSubsystem<ULoadingScreenManager>();
        if (!Experience || !Loading) { return true; }
        if (!HookedEnd)
        {
            HookedEnd = true;
            Manager = Experience;
            const TSharedRef<FExperienceFailureUIProof> Self = AsShared();
            Experience->OnExperienceEnded.AddLambda([Self]
            {
                Self->CleanupObserved = Self->CountNotices() == 0;
                if (!Self->CleanupObserved) { Self->Passed = false; Self->Failure = TEXT("Failure notice remained attached at manager EndPlay"); }
                Self->SaveReport();
                UE_LOG(LogTemp, Display, TEXT("RPG_EXPERIENCE_FAILURE_UI_PROOF CLEANUP Notices=%d"), Self->CountNotices());
            });
            if (HoldOtherLoading)
            {
                OtherLoading.Reset(ULoadingProcessTask::CreateLoadingScreenProcessTask(
                    CurrentWorld, TEXT("Failure UI proof independent loading hold")));
                if (!OtherLoading) { return Finish(TEXT("Could not create independent loading request")); }
            }
        }
        if (Experience->IsExperienceLoaded()) { return Finish(TEXT("Failure scenario unexpectedly became Ready")); }
        if (Experience->GetLoadState() != ERPGExperienceLoadState::Failed) { return true; }
        InternalReason = Experience->GetLastError();
        if (InternalReason.IsEmpty()) { return Finish(TEXT("Failed state has no diagnostic")); }
        APlayerController* PC = Instance->GetFirstLocalPlayerController(CurrentWorld);
        ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
        UGameViewportClient* Viewport = CurrentWorld->GetGameViewport();
        const TSharedPtr<FSlateUser> User = Player ? Player->GetSlateUser() : nullptr;
        if (!PC || !Player || !Viewport || !User) { return true; }
        const bool LoadingVisible = Loading->GetLoadingScreenDisplayStatus();
        if (Phase == 0)
        {
            if (!LoadingVisible) { return Finish(TEXT("Failed loading screen was not observable before dismissal")); }
            if (GetDefault<URPGExperienceSettings>()->bShowLoadingFailureNotice == ExpectNoNotice)
            {
                return Finish(TEXT("Failure notice configuration does not match scenario"));
            }
            PC->SetInputMode(FInputModeGameOnly());
            PC->SetShowMouseCursor(false);
            FSlateApplication::Get().SetUserFocus(User->GetUserIndex(), Viewport->GetGameViewportWidget(), EFocusCause::SetDirectly);
            VisibleFrame = GFrameCounter;
            Phase = 1;
            PhaseStarted = Now;
        }
        else if (Phase == 1)
        {
            if (GFrameCounter < VisibleFrame + 3) { return true; }
            if (!LoadingVisible) { return Finish(TEXT("Loading screen closed before its failure presentation was verified")); }
            URPGExperienceLoadingScreen* Screen = nullptr;
            for (TObjectIterator<URPGExperienceLoadingScreen> It; It; ++It)
            {
                if (!It->HasAnyFlags(RF_ClassDefaultObject) && It->GetWorld() == CurrentWorld && It->GetCachedWidget()) { Screen = *It; break; }
            }
            if (!Screen || !HasSafeFailureText(*Screen, true))
            {
                return Finish(TEXT("Failed loading screen has incorrect text, spinner or raw diagnostics"));
            }
            if (CountNotices(true) != 0) { return Finish(TEXT("Passive notice overlaps the global loading screen")); }
            Baseline.Read(*PC, *Viewport, *User);
            SawFailedLoading = true;
            Screenshot(TEXT("FailedLoading"));
            Phase = 2;
            PhaseStarted = Now;
        }
        else if (Phase == 2)
        {
            if (HoldOtherLoading)
            {
                if (!LoadingVisible || CountNotices(true) != 0
                    || !Loading->GetDebugReasonForShowingOrHidingLoadingScreen().Contains(TEXT("Failure UI proof independent loading hold")))
                {
                    return Finish(TEXT("Experience failure cancelled or bypassed the independent loading request"));
                }
                HoldObserved = true;
                if (Now - PhaseStarted < 3.0 || !ScreenshotsSaved()) { return true; }
                OtherLoading->Unregister();
                OtherLoading.Reset();
                VisibleFrame = 0;
                Phase = 3;
                PhaseStarted = Now;
            }
            else { VisibleFrame = 0; Phase = 3; PhaseStarted = Now; }
        }
        else if (Phase == 3)
        {
            if (LoadingVisible) { return true; }
            FClientUIInputSnapshot Current;
            Current.Read(*PC, *Viewport, *User);
            if (!Current.Equals(Baseline)) { return Finish(TEXT("Failure presentation changed gameplay input or actual focus")); }
            InputUnchanged = true;
            if (ExpectNoNotice)
            {
                if (CountNotices() != 0) { return Finish(TEXT("Notice was created while disabled")); }
                if (Now - PhaseStarted < 1.5) { return true; }
                Screenshot(TEXT("NoticeDisabled"));
                Phase = 4;
                PhaseStarted = Now;
            }
            else
            {
                URPGExperienceFailureNotice* Notice = FindNotice(*PC);
                if (!Notice || !Notice->IsVisible()) { return true; }
                if (CountNotices() != 1 || !Notice->IsVisible() || Notice->IsFocusable()
                    || Notice->GetVisibility() != ESlateVisibility::HitTestInvisible || !HasSafeFailureText(*Notice, false))
                {
                    return Finish(TEXT("Failure notice is duplicated, focusable or has unsafe text"));
                }
                if (VisibleFrame == 0) { VisibleFrame = GFrameCounter; return true; }
                if (GFrameCounter < VisibleFrame + 3) { return true; }
                NoticeObserved = true;
                Screenshot(TEXT("FailureNotice"));
                Phase = 4;
                PhaseStarted = Now;
            }
        }
        else if (Phase == 4 && Now - PhaseStarted >= 1.5)
        {
            if (LoadingVisible || CountNotices() != (ExpectNoNotice ? 0 : 1))
            {
                return Finish(TEXT("Failure presentation did not remain stable"));
            }
            FClientUIInputSnapshot Current;
            Current.Read(*PC, *Viewport, *User);
            if (!Current.Equals(Baseline)) { return Finish(TEXT("Passive notice changed input after timer refresh")); }
            if (!ScreenshotsSaved()) { return true; }
            return Finish();
        }
        return true;
    }
};

FAutoConsoleCommand FailureUIProofCommand(TEXT("RPG.Experience.FailureUIProof"),
    TEXT("Opt-in rendered verification of Experience failure UI; requires -RPGExperienceFailureUIProof."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceFailureUIProof"))) { return; }
        const TSharedRef<FExperienceFailureUIProof> Proof = MakeShared<FExperienceFailureUIProof>();
        Proof->HoldOtherLoading = FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceFailureUIProofHold"));
        Proof->ExpectNoNotice = FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceFailureUIProofNoNotice"));
        if (!FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceUIProofOutput="), Proof->Output)
            || !FPaths::IsUnderDirectory(Proof->Output, FPaths::ProjectSavedDir()))
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
        IFileManager::Get().MakeDirectory(*Proof->Output, true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Proof](float Delta) { return Proof->Tick(Delta); }));
    }));

FAutoConsoleCommand ClientUIProofCommand(TEXT("RPG.Experience.ClientUIProof"),
    TEXT("Opt-in verification of the common client development map; requires -RPGExperienceUIProof."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("RPGExperienceUIProof"))) { return; }
        TSharedRef<FExperienceClientUIProof> Proof = MakeShared<FExperienceClientUIProof>();
        if (!FParse::Value(FCommandLine::Get(), TEXT("RPGExperienceUIProofOutput="), Proof->Output)
            || !FPaths::IsUnderDirectory(Proof->Output, FPaths::ProjectSavedDir()))
        {
            UE_LOG(LogTemp, Error, TEXT("RPG_EXPERIENCE_UI_PROOF output must be under this project's Saved directory"));
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
        IFileManager::Get().MakeDirectory(*Proof->Output, true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
            [Proof](float Delta) { return Proof->Tick(Delta); }));
    }));
}
#endif
