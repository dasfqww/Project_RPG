// Opt-in integration checks on the isolated camera development Experience; excluded from Shipping.
#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/RPGPlayerCameraManager.h"
#include "Camera/RPGUICameraManagerComponent.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Definition/RPGUserFacingExperienceDefinition.h"
#include "CommonSessionSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/InputDeviceLibrary.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "InputMappingContext.h"
#include "Manager/Component/RPGExperienceManagerComponent.h"
#include "Manager/UIManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "UI/Option/RPGOptionMenu.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
struct FModularizationProof
{
    FString Output, Mode;
    double Started = FPlatformTime::Seconds(), StepStarted = Started;
    int32 Step = 0, Changes = 0;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ULocalPlayer> Player;
    TWeakObjectPtr<ARPGPlayerController> SecondaryPlayer;
    TWeakObjectPtr<UUIManager> PrimaryUI;
    TWeakObjectPtr<UActorComponent> Widgets, Options, Presentation;
    TWeakObjectPtr<AActor> EngineTarget;
    TWeakObjectPtr<ACameraActor> OriginalCamera;
    TWeakObjectPtr<URPGOptionMenu> FallbackMenu;
    FDelegateHandle ChangedHandle;
    TArray<TSharedPtr<FJsonValue>> Checks;
    FRPGInputOptionBinding OriginalBinding;
    FVector OriginalCameraLocation = FVector::ZeroVector;
    bool OriginalCursor = false;

    ~FModularizationProof() { IRPGInputOptionsRuntime::OnMappingsChanged().Remove(ChangedHandle); }

    void Mark(const TCHAR* Name)
    {
        Checks.Add(MakeShared<FJsonValueString>(Name));
        UE_LOG(LogTemp, Display, TEXT("RPG_MODULE_PROOF CHECK=%s"), Name);
    }

    bool Finish(const FString& Failure = FString())
    {
        IRPGInputOptionsRuntime::OnMappingsChanged().Remove(ChangedHandle);
        ChangedHandle.Reset();
        const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetBoolField(TEXT("Passed"), Failure.IsEmpty());
        Report->SetStringField(TEXT("Failure"), Failure);
        Report->SetStringField(TEXT("Mode"), Mode);
        Report->SetNumberField(TEXT("Step"), Step);
        Report->SetArrayField(TEXT("Checks"), Checks);
        Report->SetBoolField(TEXT("PhysicalKeyboardVerified"), false);
        Report->SetBoolField(TEXT("SessionCreated"), false);
        const FConfigBranch* Settings = GConfig ? GConfig->FindBranchWithNoReload(FName(TEXT("GameUserSettings")), GGameUserSettingsIni) : nullptr;
        Report->SetStringField(TEXT("GameUserSettingsIni"), Settings ? Settings->IniPath : FString());
        Report->SetStringField(TEXT("ConfigCacheKey"), GGameUserSettingsIni);
        FString Json;
        FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
        const bool Saved = FFileHelper::SaveStringToFile(Json, *FPaths::Combine(Output, TEXT("Report.json")));
        UE_LOG(LogTemp, Display, TEXT("RPG_MODULE_PROOF FINISHED Mode=%s Passed=%d Failure=%s"), *Mode, Failure.IsEmpty() && Saved, *Failure);
        FPlatformMisc::RequestExitWithStatus(false, Failure.IsEmpty() && Saved ? 0 : 1);
        return false;
    }

    void Advance() { ++Step; StepStarted = FPlatformTime::Seconds(); }

    UActorComponent* FindComponent(AActor& Owner, const TCHAR* ClassPath) const
    {
        UClass* Class = FindObject<UClass>(nullptr, ClassPath);
        return Class ? Owner.FindComponentByClass(Class) : nullptr;
    }

    bool HasBindings(ARPGHUD& HUD) const
    {
        UActorComponent* Component = FindComponent(HUD, TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsMenuComponent"));
        UFunction* Function = Component ? Component->FindFunction(TEXT("HasShortcutBindings")) : nullptr;
        if (!Function) { return false; }
        TArray<uint8> Parameters;
        Parameters.SetNumZeroed(Function->ParmsSize);
        Component->ProcessEvent(Function, Parameters.GetData());
        const FBoolProperty* Value = CastField<FBoolProperty>(Function->GetReturnProperty());
        return Value && Value->GetPropertyValue_InContainer(Parameters.GetData());
    }

    int32 CountWidgets(APlayerController& PC, bool Hints) const
    {
        int32 Count = 0;
        for (TObjectIterator<UUserWidget> It; It; ++It)
        {
            if (!IsValid(*It) || It->IsTemplate() || It->GetOwningPlayer() != &PC || !It->IsInViewport()) { continue; }
            if (Hints ? It->GetClass()->GetPathName() == TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint")
                      : It->IsA<URPGOptionMenu>()) { ++Count; }
        }
        return Count;
    }

    int32 CountShortcutInputs(APlayerController& PC, ARPGHUD& HUD) const
    {
        int32 Count = 0;
        for (TObjectIterator<UEnhancedInputComponent> It; It; ++It)
        {
            if (IsValid(*It) && It->GetOuter() == &HUD && PC.IsInputComponentInStack(*It)) { ++Count; }
        }
        return Count;
    }

    bool ReadBinding(IRPGInputOptionsRuntime& Runtime, ARPGPlayerController& PC, FRPGInputOptionBinding& Result) const
    {
        TArray<FRPGInputOptionBinding> Rows;
        Runtime.BuildRows(&PC, nullptr, Rows);
        for (const auto& Row : Rows)
        {
            if (Row.bIsAdditionalMapping && Row.bCanRemap && Row.DefaultKey == EKeys::F10
                && Row.SourceMappingContext.IsValid()
                && Row.SourceMappingContext->GetPathName() == TEXT("/RPGInputMapping/Input/IMC_OptionsShortcut.IMC_OptionsShortcut"))
            {
                Result = Row;
                return true;
            }
        }
        return false;
    }

    bool SourceUnchanged(const FRPGInputOptionBinding& Binding) const
    {
        const UInputMappingContext* Source = Binding.SourceMappingContext.Get();
        if (!Source || Source == Binding.AppliedMappingContext.Get()) { return false; }
        for (const FEnhancedActionKeyMapping& Mapping : Source->GetMappings())
        {
            if (Mapping.Action.Get() == Binding.Action.Get() && Mapping.Key == EKeys::F10) { return true; }
        }
        return false;
    }

    void Key(UGameViewportClient& Viewport, FKey Value, EInputEvent Event) const
    {
        const FInputKeyEventArgs Args(Viewport.Viewport, UInputDeviceLibrary::GetDefaultInputDevice(),
            Value, Event, FPlatformTime::Cycles64());
        Viewport.InputKey(Args);
    }

    bool CheckFrontend()
    {
        TStrongObjectPtr<URPGUserFacingExperienceDefinition> Definition(NewObject<URPGUserFacingExperienceDefinition>());
        Definition->MapID = FPrimaryAssetId(TEXT("Map:/Game/Maps/DevMap"));
        Definition->ExperienceID = FPrimaryAssetId(TEXT("RPGExperienceDefinition:DA_ClientPresentationAndOptionsHint"));
        Definition->ExtraArgs.Add(TEXT("rpgexperience"), TEXT("OldSelection"));
        Definition->ExtraArgs.Add(TEXT("RPGEXPERIENCE"), TEXT("OtherSelection"));
        Definition->ExtraArgs.Add(TEXT("OtherOption"), TEXT("Preserved"));
        const int32 AuthoredCount = Definition->ExtraArgs.Num();
        const FString AuthoredFallback = Definition->ExtraArgs.FindRef(TEXT("rpgexperience"));
        TStrongObjectPtr<UCommonSession_HostSessionRequest> Request(Definition->CreateHostingRequest(nullptr));
        if (!Request.IsValid()) { return false; }
        int32 Selections = 0;
        for (const auto& Pair : Request->ExtraArgs)
        {
            if (Pair.Key.Equals(TEXT("RPGExperience"), ESearchCase::IgnoreCase)) { ++Selections; }
        }
        const FURL URL(nullptr, *Request->ConstructTravelURL(), TRAVEL_Absolute);
        if (Selections != 1 || FString(URL.GetOption(TEXT("RPGExperience="), TEXT(""))) != Definition->ExperienceID.ToString()
            || Request->ExtraArgs.FindRef(TEXT("Experience")) != TEXT("DA_ClientPresentationAndOptionsHint")
            || Request->ExtraArgs.FindRef(TEXT("OtherOption")) != TEXT("Preserved")
            || Definition->ExtraArgs.Num() != AuthoredCount) { return false; }
        Definition->ExperienceID = FPrimaryAssetId();
        Request.Reset(Definition->CreateHostingRequest(nullptr));
        return Request.IsValid() && Request->ExtraArgs.FindRef(TEXT("rpgexperience")) == AuthoredFallback;
    }

    bool Tick(float)
    {
        if (FPlatformTime::Seconds() - Started > 90.0) { return Finish(TEXT("Timed out waiting for modularization checks")); }
        if (!World.IsValid() && GEngine)
        {
            for (const FWorldContext& Context : GEngine->GetWorldContexts())
            {
                UWorld* Candidate = Context.World();
                if (Candidate && Candidate->IsGameWorld() && Candidate->GetPackage()->GetName()
                    == TEXT("/Game/System/Experiences/Development/Camera/Maps/L_CameraExperienceDevelopment")) { World = Candidate; break; }
            }
        }
        UWorld* CurrentWorld = World.Get();
        if (!CurrentWorld) { return true; }
        if (CurrentWorld->bIsTearingDown) { return Finish(TEXT("World ended before checks completed")); }
        UGameInstance* Instance = CurrentWorld->GetGameInstance();
        ARPGPlayerController* PC = Instance ? Cast<ARPGPlayerController>(Instance->GetFirstLocalPlayerController()) : nullptr;
        ARPGHUD* HUD = PC ? Cast<ARPGHUD>(PC->GetHUD()) : nullptr;
        AGameStateBase* State = CurrentWorld->GetGameState();
        URPGExperienceManagerComponent* Experience = State ? State->FindComponentByClass<URPGExperienceManagerComponent>() : nullptr;
        if (Experience && Experience->GetLoadState() == ERPGExperienceLoadState::Failed) { return Finish(Experience->GetLastError()); }
        ARPGPlayerCameraManager* Manager = PC ? Cast<ARPGPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
        URPGUICameraManagerComponent* Camera = Manager ? Manager->GetUICameraComponent() : nullptr;
        UGameViewportClient* Viewport = CurrentWorld->GetGameViewport();
        IRPGInputOptionsRuntime* Runtime = IRPGInputOptionsRuntime::Get();
        if (!Experience || !Experience->IsExperienceLoaded() || !PC || !HUD || !State || !Camera || !Viewport || !Runtime
            || !HUD->GetOptionMenuWidget() || (Step == 0 && !HasBindings(*HUD))) { return true; }
        if (Step != 0 && FPlatformTime::Seconds() - StepStarted < 0.35) { return true; }
        if (Step == 0)
        {
            if (!Camera->GetPresentationViewTarget() || CountWidgets(*PC, true) != 1 || CountShortcutInputs(*PC, *HUD) != 1) { return true; }
            FMinimalViewInfo ExpectedView;
            Camera->GetPresentationViewTarget()->CalcCamera(0.f, ExpectedView);
            if (!Manager->GetCameraLocation().Equals(ExpectedView.Location, 1.0)) { return true; }
            Player = PC->GetLocalPlayer();
            ChangedHandle = IRPGInputOptionsRuntime::OnMappingsChanged().AddLambda([this](ULocalPlayer* ChangedPlayer, UWorld* ChangedWorld)
            {
                if (ChangedPlayer == Player.Get() && ChangedWorld == World.Get()) { ++Changes; }
            });
            FRPGInputOptionBinding Binding;
            if (!ReadBinding(*Runtime, *PC, Binding) || !SourceUnchanged(Binding)) { return Finish(TEXT("Editable feature-owned shortcut is missing")); }
            const FKey Expected = Mode == TEXT("Restore") ? EKeys::F8 : EKeys::F10;
            if (Binding.CurrentKey != Expected) { return Finish(TEXT("Restarted shortcut differs from expected persisted key")); }
            Mark(*FString::Printf(TEXT("RestartKey=%s"), *Binding.CurrentKey.ToString()));
            if (Mode == TEXT("VerifyDefault")) { return Finish(); }
            if (Mode == TEXT("Players"))
            {
                PrimaryUI = UUIManager::Get(PC);
                SecondaryPlayer = Cast<ARPGPlayerController>(UGameplayStatics::CreatePlayer(CurrentWorld, -1, true));
                if (!SecondaryPlayer.IsValid()) { return Finish(TEXT("Could not create a second local player")); }
                Step = 20;
                StepStarted = FPlatformTime::Seconds();
                return true;
            }
            if (Mode == TEXT("Restore"))
            {
                TArray<FRPGInputOptionBinding> Defaults;
                Runtime->BuildDefaultRestoration(PC, Defaults);
                FText Error;
                if (Defaults.IsEmpty() || !Runtime->TryRestoreDefaults(PC, Defaults, Error)
                    || !ReadBinding(*Runtime, *PC, Binding) || Binding.CurrentKey != EKeys::F10 || Changes == 0)
                { return Finish(TEXT("Unified defaults failed: ") + Error.ToString()); }
                Mark(TEXT("UnifiedDefaultsAndNotification"));
                return Finish();
            }
            if (!CheckFrontend()) { return Finish(TEXT("Front-end URL selection contract failed")); }
            Mark(TEXT("FrontendFullIdAndPrecedence"));
            OriginalBinding = Binding;
            EngineTarget = PC->GetViewTarget();
            OriginalCamera = Camera->GetPresentationViewTarget();
            OriginalCameraLocation = Manager->GetCameraLocation();
            OriginalCursor = PC->bShowMouseCursor;
            Widgets = FindComponent(*HUD, TEXT("/Script/RPGClientUIRuntime.RPGClientUIWidgetComponent"));
            Options = FindComponent(*HUD, TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsMenuComponent"));
            TArray<UActorComponent*> Components;
            State->GetComponents(Components);
            for (UActorComponent* Component : Components)
            {
                const FNameProperty* Tag = FindFProperty<FNameProperty>(Component->GetClass(), TEXT("CameraTargetActorTag"));
                if (Tag && !Tag->GetPropertyValue_InContainer(Component).IsNone()) { Presentation = Component; break; }
            }
            if (!Widgets.IsValid() || !Options.IsValid() || !Presentation.IsValid()) { return Finish(TEXT("Expected feature components are missing")); }
            Widgets->UnregisterComponent();
            if (CountWidgets(*PC, true) != 0) { return Finish(TEXT("Unregistered client widgets remained on screen")); }
            Widgets->RegisterComponent();
            Advance();
        }
        else if (Step == 1)
        {
            if (CountWidgets(*PC, true) != 1) { return Finish(TEXT("Client widgets did not restore exactly once")); }
            Mark(TEXT("ClientWidgetUnregisterAndRestore"));
            Options->UnregisterComponent();
            if (HasBindings(*HUD) || CountShortcutInputs(*PC, *HUD) != 0 || CountWidgets(*PC, false) != 1)
            { return Finish(TEXT("Options provider/input did not release to one default menu")); }
            FallbackMenu = HUD->GetOptionMenuWidget();
            Options->RegisterComponent();
            Advance();
        }
        else if (Step == 2)
        {
            if (!HasBindings(*HUD) || CountShortcutInputs(*PC, *HUD) != 1 || CountWidgets(*PC, false) != 1
                || HUD->GetOptionMenuWidget() != FallbackMenu.Get()) { return Finish(TEXT("Options re-registration duplicated or lost the fallback menu")); }
            Mark(TEXT("OptionsInputReleaseAndFallbackReuse"));
            Presentation->UnregisterComponent();
            if (Camera->GetPresentationViewTarget()) { return Finish(TEXT("Camera request survived unregister")); }
            Advance();
        }
        else if (Step == 3)
        {
            if (PC->GetViewTarget() != EngineTarget.Get() || Manager->GetCameraLocation().Equals(OriginalCameraLocation, 1.0))
            { return Finish(TEXT("Camera did not return to its unchanged engine target")); }
            Mark(TEXT("CameraFallback"));
            Presentation->RegisterComponent();
            Advance();
        }
        else if (Step == 4)
        {
            if (Camera->GetPresentationViewTarget() != OriginalCamera.Get() || PC->GetViewTarget() != EngineTarget.Get()
                || !Manager->GetCameraLocation().Equals(OriginalCameraLocation, 1.0)) { return Finish(TEXT("Presentation camera did not restore")); }
            ACameraActor* First = CurrentWorld->SpawnActor<ACameraActor>();
            ACameraActor* Second = CurrentWorld->SpawnActor<ACameraActor>();
            if (!First || !Second) { return Finish(TEXT("Could not create transient priority cameras")); }
            const bool Priority = Camera->AddPresentationRequest(First, First, 20)
                && Camera->AddPresentationRequest(Second, Second, 20) && Camera->GetPresentationViewTarget() == Second
                && Camera->AddPresentationRequest(First, First, 20) && Camera->GetPresentationViewTarget() == Second
                && Camera->AddPresentationRequest(First, First, 21) && Camera->GetPresentationViewTarget() == First;
            Camera->RemovePresentationRequest(First);
            const bool OtherPreserved = Camera->GetPresentationViewTarget() == Second;
            First->Destroy();
            Second->Destroy();
            if (!Priority || !OtherPreserved || Camera->GetPresentationViewTarget() != OriginalCamera.Get()) { return Finish(TEXT("Camera priority or independent request release failed")); }
            Mark(TEXT("CameraPriorityAndIndependentRelease"));
            FText Error;
            if (!Runtime->TryApplyBinding(PC, OriginalBinding, EKeys::F8, Error) || Changes == 0)
            { return Finish(TEXT("Shortcut remap failed: ") + Error.ToString()); }
            if (Runtime->TryApplyBinding(PC, OriginalBinding, EKeys::F12, Error)) { return Finish(TEXT("Stale key selection was accepted")); }
            Advance();
        }
        else if (Step == 5)
        {
            FRPGInputOptionBinding Binding;
            if (!ReadBinding(*Runtime, *PC, Binding) || Binding.CurrentKey != EKeys::F8 || !SourceUnchanged(Binding))
            { return Finish(TEXT("Remap changed the authored context or lost the live key")); }
            Mark(TEXT("RemapNotificationAndSourcePreservation"));
            PC->SetInputMode(FInputModeGameOnly());
            Key(*Viewport, EKeys::F10, IE_Pressed);
            Advance();
        }
        else if (Step == 6)
        {
            Key(*Viewport, EKeys::F10, IE_Released);
            if (HUD->bIsOptionMenuVisible) { return Finish(TEXT("Old shortcut still opened options")); }
            Key(*Viewport, EKeys::F8, IE_Pressed);
            Advance();
        }
        else if (Step == 7)
        {
            Key(*Viewport, EKeys::F8, IE_Released);
            if (!HUD->bIsOptionMenuVisible || !HUD->GetOptionMenuWidget()->IsVisible()) { return Finish(TEXT("New shortcut did not open options")); }
            Advance();
        }
        else if (Step == 8)
        {
            Key(*Viewport, EKeys::F8, IE_Pressed);
            Advance();
        }
        else if (Step == 20)
        {
            ARPGPlayerController* Other = SecondaryPlayer.Get();
            ARPGHUD* OtherHUD = Other ? Cast<ARPGHUD>(Other->GetHUD()) : nullptr;
            FRPGInputOptionBinding FirstBinding, OtherBinding;
            if (!Other || !OtherHUD || !HasBindings(*OtherHUD) || CountWidgets(*Other, true) != 1
                || !ReadBinding(*Runtime, *PC, FirstBinding) || !ReadBinding(*Runtime, *Other, OtherBinding))
            {
                if (FPlatformTime::Seconds() - StepStarted > 10.0) { return Finish(TEXT("Second local player's feature UI/input did not become ready")); }
                return true;
            }
            if (Instance->GetLocalPlayers().Num() != 2 || UUIManager::Get(PC) != PrimaryUI.Get()
                || UUIManager::Get(PC) == UUIManager::Get(Other)
                || UUIManager::Get(PC)->GetOuter() != PC->GetLocalPlayer()
                || UUIManager::Get(Other)->GetOuter() != Other->GetLocalPlayer()
                || FirstBinding.AppliedMappingContext == OtherBinding.AppliedMappingContext)
            { return Finish(TEXT("Local players shared a UI manager or editable input copy")); }
            FText Error;
            if (!Runtime->TryApplyBinding(PC, FirstBinding, EKeys::F8, Error)
                || !ReadBinding(*Runtime, *Other, OtherBinding) || OtherBinding.CurrentKey != EKeys::F10)
            { return Finish(TEXT("Primary remap changed the other local player: ") + Error.ToString()); }
            TArray<FRPGInputOptionBinding> Defaults;
            Runtime->BuildDefaultRestoration(PC, Defaults);
            if (!Runtime->TryRestoreDefaults(PC, Defaults, Error))
            { return Finish(TEXT("Primary defaults conflicted with another local player's mappings: ") + Error.ToString()); }
            Mark(TEXT("LocalPlayerManagersAndInputCopiesIsolated"));
            UGameplayStatics::RemovePlayer(Other, true);
            Advance();
        }
        else if (Step == 21)
        {
            int32 Hints = 0, Menus = 0;
            for (TObjectIterator<UUserWidget> It; It; ++It)
            {
                if (!IsValid(*It) || It->IsTemplate() || It->GetWorld() != CurrentWorld || !It->IsInViewport()) { continue; }
                if (It->GetClass()->GetPathName() == TEXT("/Script/RPGOptionsUIRuntime.RPGOptionsShortcutHint")) { ++Hints; }
                if (It->IsA<URPGOptionMenu>()) { ++Menus; }
            }
            FRPGInputOptionBinding Binding;
            if (Instance->GetLocalPlayers().Num() != 1 || Hints != 1 || Menus != 1 || !HasBindings(*HUD)
                || UUIManager::Get(PC) != PrimaryUI.Get() || !ReadBinding(*Runtime, *PC, Binding) || Binding.CurrentKey != EKeys::F10)
            { return Finish(TEXT("Removing the secondary local player damaged primary ownership or left widgets")); }
            Mark(TEXT("LocalPlayerRemovalPreservesPrimaryOwnership"));
            return Finish();
        }
        else if (Step == 9)
        {
            Key(*Viewport, EKeys::F8, IE_Released);
            if (HUD->bIsOptionMenuVisible || HUD->GetOptionMenuWidget()->IsVisible() || PC->bShowMouseCursor != OriginalCursor)
            { return Finish(TEXT("New shortcut did not close options and restore cursor")); }
            Mark(TEXT("RemappedKeyViewportRoutingAndCursorRestore"));
            return Finish();
        }
        return true;
    }
};

FAutoConsoleCommand ModularizationProofCommand(TEXT("RPG.Experience.ModularizationProof"),
    TEXT("Opt-in isolated Experience integration checks; requires -RPGModularizationProof."),
    FConsoleCommandDelegate::CreateLambda([]
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("RPGModularizationProof"))) { return; }
        const TSharedRef<FModularizationProof> Proof = MakeShared<FModularizationProof>();
        FParse::Value(FCommandLine::Get(), TEXT("RPGModularizationProofMode="), Proof->Mode);
        if (!FParse::Value(FCommandLine::Get(), TEXT("RPGModularizationProofOutput="), Proof->Output)
            || !FPaths::IsUnderDirectory(Proof->Output, FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved")))
            || (Proof->Mode != TEXT("Write") && Proof->Mode != TEXT("Restore") && Proof->Mode != TEXT("VerifyDefault") && Proof->Mode != TEXT("Players")))
        { FPlatformMisc::RequestExitWithStatus(false, 1); return; }
        IFileManager::Get().MakeDirectory(*Proof->Output, true);
        FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Proof](float Delta) { return Proof->Tick(Delta); }));
    }));
}
#endif
