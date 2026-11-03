#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "GameInstance/RPGGameInstance.h"
#include "Manager/SoundManager.h"
#include "Settings/RPGGameUserSettings.h"
#include "UI/Option/SoundOptionMenu.h"
#include "UI/Option/RPGSoundOptionsRuntime.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "UI/CheckBox/RPGCheckBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

struct FRPGSoundOptionsTestAccess
{
    static void Prepare(USoundOptionMenu& Menu)
    {
        Menu.MasterVolumeSlider = NewObject<USlider>(&Menu);
        Menu.BGMVolumeSlider = NewObject<USlider>(&Menu);
        Menu.EffectVolumeSlider = NewObject<USlider>(&Menu);
        Menu.MasterMuteCheckBox = NewObject<UCheckBox>(&Menu);
        Menu.BGMMuteCheckBox = NewObject<URPGCheckBox>(&Menu);
        Menu.EffectMuteCheckBox = NewObject<URPGCheckBox>(&Menu);
        Menu.bMenuConstructed = true;
        Menu.InitWidgetMaps();
        Menu.InitializeFromSavedSettings();
    }
    static USlider* Master(USoundOptionMenu& Menu) { return Menu.MasterVolumeSlider; }
};

namespace RPGSoundOptionsTests
{
struct FFixture
{
    TStrongObjectPtr<URPGGameUserSettings> Settings{NewObject<URPGGameUserSettings>()};
    TStrongObjectPtr<UGameUserSettings> Previous{GEngine->GameUserSettings.Get()};
    TStrongObjectPtr<URPGGameInstance> Instance;
    UWorld* World = nullptr;
    USoundManager* Manager = nullptr;
    bool Initialize()
    {
        GEngine->GameUserSettings = Settings.Get();
        Settings->MasterVolume = .4f;
        Settings->InitializeOptionCommitState();
        Instance.Reset(NewObject<URPGGameInstance>(GEngine));
        Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/SoundOptions_%s"), *FGuid::NewGuid().ToString())));
        World = Instance->GetWorld();
        if (!World) return false;
        Manager = Instance->GetSubsystem<USoundManager>();
        if (!Manager) return false;
        Manager->Init(World);
        return true;
    }
    void ConfigureRouting()
    {
        auto Pair = [&]()
        {
            FSoundClassPair P;
            P.SoundClass = NewObject<USoundClass>(Instance.Get());
            P.SoundMix = NewObject<USoundMix>(Instance.Get());
            return P;
        };
        auto* MapProperty = FindFProperty<FMapProperty>(URPGGameInstance::StaticClass(), TEXT("SoundClassMap"));
        auto* MasterProperty = FindFProperty<FStructProperty>(URPGGameInstance::StaticClass(), TEXT("MasterSound"));
        check(MapProperty && MasterProperty);
        auto& Map = *MapProperty->ContainerPtrToValuePtr<TMap<ESoundType, FSoundClassPair>>(Instance.Get());
        Map.Add(ESoundType::BGM, Pair());
        Map.Add(ESoundType::Effect, Pair());
        *MasterProperty->ContainerPtrToValuePtr<FSoundClassPair>(Instance.Get()) = Pair();
    }
    ~FFixture()
    {
        if (Instance.IsValid()) Instance->Shutdown();
        RPGTestWorld::Destroy(World);
        GEngine->GameUserSettings = Previous.Get();
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSoundRoutingFailureTest, "ProjectRPG.Options.Sound.MissingRoutingDoesNotMutateOrNotify",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSoundRoutingFailureTest::RunTest(const FString&)
{
    using namespace RPGSoundOptionsTests;
    FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
    int32 Notifications = 0;
    F.Settings->OnSoundSettingsChanged.AddLambda([&]() { ++Notifications; });
    FRPGSoundSettings Requested = F.Settings->GetSoundOptions();
    Requested.MasterVolume = .8f;
    FText Error;
    TestFalse(TEXT("Unconfigured channels rejected"), F.Manager->ValidateSoundRouting(Error));
    TestFalse(TEXT("Manager rejects before mutation"), F.Manager->ApplySoundSettings(Requested));
    TestEqual(TEXT("Old volume preserved"), F.Settings->MasterVolume, .4f);
    TestEqual(TEXT("No success notification"), Notifications, 0);
    IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get();
    if (!TestNotNull(TEXT("Runtime"), Runtime)) return false;
    TestFalse(TEXT("Runtime exposes routing error"), Runtime->TryApplySettings(Requested, F.World, Error));
    TestFalse(TEXT("User can see a reason"), Error.IsEmpty());
    F.ConfigureRouting();
    TestTrue(TEXT("Configured routing applies"), Runtime->TryApplySettings(Requested, F.World, Error));
    TestEqual(TEXT("Live volume changed"), F.Settings->MasterVolume, .8f);
    TestEqual(TEXT("Only successful request notified"), Notifications, 1);
    F.Settings->OnSoundSettingsChanged.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSoundUIFailureTest, "ProjectRPG.Options.Sound.RejectedEditRestoresControls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSoundUIFailureTest::RunTest(const FString&)
{
    using namespace RPGSoundOptionsTests;
    FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
    TStrongObjectPtr<USoundOptionMenu> Menu(NewObject<USoundOptionMenu>(F.World));
    Menu->Initialize();
    FRPGSoundOptionsTestAccess::Prepare(*Menu);
    auto* Slider = FRPGSoundOptionsTestAccess::Master(*Menu);
    Slider->SetValue(.8f);
    Menu->OnMasterVolumeChanged(.8f);
    TestTrue(TEXT("Rejected live edit reports failure"), Menu->DidLastSaveFail());
    TestFalse(TEXT("Failure is visible"), Menu->LastSaveMessage.IsEmpty());
    TestEqual(TEXT("Control restored to actual value"), Slider->GetValue(), .4f);
    TestEqual(TEXT("State unchanged"), F.Settings->MasterVolume, .4f);
    F.ConfigureRouting();
    Slider->SetValue(.7f);
    Menu->OnMasterVolumeChanged(.7f);
    TestFalse(TEXT("Next successful edit clears failure"), Menu->DidLastSaveFail());
    TestEqual(TEXT("Successful edit changes state"), F.Settings->MasterVolume, .7f);
    Menu->NativeDestruct();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSoundPersistenceTest, "ProjectRPG.Options.Sound.JSONCommitAndWriteFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSoundPersistenceTest::RunTest(const FString&)
{
    // This integration case deliberately exercises the fixed legacy JSON filenames.
    // Require a private runtime; never touch a developer's existing preferences from a generic test run.
    if (!FParse::Param(FCommandLine::Get(), TEXT("RPGOptionsIsolatedTests")))
    {
        AddWarning(TEXT("JSON integration requires -RPGOptionsIsolatedTests in an isolated runtime."));
        return true;
    }
    using namespace RPGSoundOptionsTests;
    FFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
    F.ConfigureRouting();
    const FString Saved = FPaths::ProjectSavedDir();
    const FString JSON = Saved / TEXT("sound_options.json");
    IFileManager& Files = IFileManager::Get();
    if (!TestFalse(TEXT("Private runtime must start without sound JSON"), Files.FileExists(*JSON))) return false;
    TGuardValue<FString> GamePath(GGameUserSettingsIni, GGameUserSettingsIni);
    TGuardValue<FString> EditorPath(GEditorSettingsIni, Saved / TEXT("OptionsIntegrationEditor.ini"));
    IRPGSoundOptionsRuntime* Runtime = IRPGSoundOptionsRuntime::Get();
    if (!TestNotNull(TEXT("Runtime"), Runtime)) return false;
    FText Error;
    FRPGSoundSettings Requested = F.Settings->GetSoundOptions();
    Requested.MasterVolume = .7f;
    TestTrue(TEXT("Save applies and commits JSON"), Runtime->TrySaveSettings(Requested, F.World, Error));
    TestTrue(TEXT("JSON written"), Files.FileExists(*JSON));
    // A directory at the primary filename forces replacement failure, independent of user permissions.
    const FString Held = JSON + TEXT(".held");
    if (!TestTrue(TEXT("Retain owned JSON"), Files.Move(*Held, *JSON))) return false;
    Files.MakeDirectory(*JSON);
    AddExpectedError(TEXT("Could not save options Stage=ReplacePrimary"), EAutomationExpectedErrorFlags::Contains, 1);
    Requested.MasterVolume = .9f;
    TestFalse(TEXT("Write failure is reported"), Runtime->TrySaveSettings(Requested, F.World, Error));
    TestFalse(TEXT("Write failure has a reason"), Error.IsEmpty());
    TestEqual(TEXT("Live audio remains applied on save failure"), F.Settings->MasterVolume, .9f);
    F.Settings->SaveSettings();
    GConfig->Flush(false, GGameUserSettingsIni);
    FConfigFile Disk; Disk.Read(GConfig->FindBranch(*GGameUserSettingsIni, GGameUserSettingsIni)->IniPath);
    FString Raw;
    FFileHelper::LoadFileToString(Raw, *GConfig->FindBranch(*GGameUserSettingsIni, GGameUserSettingsIni)->IniPath);
    UE_LOG(LogTemp, Display, TEXT("OPTIONS_JSON_INI_DIAG %s: %s"), *GGameUserSettingsIni, *Raw);
    float Volume = 0.f;
    Disk.GetFloat(TEXT("/Script/Project_RPG.RPGGameUserSettings"), TEXT("MasterVolume"), Volume);
    TestEqual(TEXT("Later generic save still uses last successful JSON snapshot"), Volume, .7f);
    Files.DeleteDirectory(*JSON, false, false);
    TestTrue(TEXT("Restore owned saved JSON"), Files.Move(*JSON, *Held));
    Runtime->RestoreSavedSettings(F.World);
    TestEqual(TEXT("Authoritative saved value restored"), F.Settings->MasterVolume, .7f);
    return true;
}
#endif
