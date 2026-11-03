#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Settings/RPGGameUserSettings.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace RPGOptionsCommitTests
{
bool IsIsolated(FAutomationTestBase& Test)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RPGOptionsIsolatedTests"))
        && FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()).Replace(TEXT("\\"), TEXT("/")).Contains(TEXT("/Saved/SLC/"))) return true;
    Test.AddWarning(TEXT("INI regression requires a private Saved/SLC runtime and -RPGOptionsIsolatedTests."));
    return false;
}
struct FFixture
{
    TStrongObjectPtr<URPGGameUserSettings> Settings{NewObject<URPGGameUserSettings>()};
    FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tests/OptionsCommit") / FGuid::NewGuid().ToString());
    // UE reloads the generated GameUserSettings path while saving window positions.
    // This is safe only in the private runtime required above; redirecting the global alone is insufficient.
    FString GameIni = GGameUserSettingsIni;
    FString EditorIni = Root / TEXT("EditorSettings.ini");
    TGuardValue<FString> GamePath{GGameUserSettingsIni, GameIni};
    TGuardValue<FString> EditorPath{GEditorSettingsIni, EditorIni};
    FFixture()
    {
        IFileManager::Get().MakeDirectory(*Root, true);
        Settings->SetScreenResolution(FIntPoint(1280, 720));
        Settings->SetFullscreenMode(EWindowMode::Windowed);
        Settings->SetVSyncEnabled(false);
        Settings->ConfirmVideoMode();
        Settings->MasterVolume = .4f;
        Settings->BGMVolume = .5f;
        Settings->EffectVolume = .6f;
        Settings->bBGMMuted = true;
        Settings->CommitSoundOptions(Settings->GetSoundOptions());
        Settings->CommitGraphicsOptions();
    }
    ~FFixture()
    {
        GConfig->UnloadFile(GameIni);
        GConfig->UnloadFile(EditorIni);
        // Unique ordinary files only. A private runner also removes the containing runtime.
        IFileManager::Get().Delete(*EditorIni);
        IFileManager::Get().DeleteDirectory(*Root, false, false);
    }
    FConfigFile ReadDisk()
    {
        GConfig->Flush(false, GameIni);
        FString Raw;
        const FString ActualIni = GConfig->FindBranch(*GGameUserSettingsIni, GGameUserSettingsIni)->IniPath;
        FFileHelper::LoadFileToString(Raw, *ActualIni);
        UE_LOG(LogTemp, Display, TEXT("OPTIONS_INI_DIAG %s: %s"), *GameIni, *Raw);
        FConfigFile File;
        File.Read(ActualIni);
        return File;
    }
    static constexpr const TCHAR* Section = TEXT("/Script/Project_RPG.RPGGameUserSettings");
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOptionsUncommittedTest, "ProjectRPG.Options.Commit.GenericSaveKeepsCommittedValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOptionsUncommittedTest::RunTest(const FString&)
{
    using namespace RPGOptionsCommitTests;
    if (!IsIsolated(*this)) return true;
    FFixture F;
    F.Settings->MasterVolume = .8f;
    F.Settings->BGMVolume = .9f;
    F.Settings->bBGMMuted = false;
    F.Settings->SetScreenResolution(FIntPoint(1600, 900));
    F.Settings->SetVSyncEnabled(true);
    F.Settings->ConfirmVideoMode(); // Confirmation can precede a failing JSON write.
    F.Settings->SaveSettings(); // Same public entry point used by unrelated settings saves.
    const FConfigFile Disk = F.ReadDisk();
    float Value = 0.f; int32 Width = 0; bool Muted = false;
    TestTrue(TEXT("Disk contains committed volume"), Disk.GetFloat(FFixture::Section, TEXT("MasterVolume"), Value));
    TestEqual(TEXT("Unconfirmed volume must not leak to INI"), Value, .4f);
    Disk.GetFloat(FFixture::Section, TEXT("BGMVolume"), Value);
    TestEqual(TEXT("Other audio channels are also isolated"), Value, .5f);
    Disk.GetBool(FFixture::Section, TEXT("bBGMMuted"), Muted);
    TestTrue(TEXT("Committed mute is preserved"), Muted);
    Disk.GetInt(FFixture::Section, TEXT("ResolutionSizeX"), Width);
    TestEqual(TEXT("Failed/unconfirmed display save cannot leak later"), Width, 1280);
    Disk.GetInt(FFixture::Section, TEXT("LastUserConfirmedResolutionSizeX"), Width);
    TestEqual(TEXT("Confirmation metadata also remains committed"), Width, 1280);
    TestEqual(TEXT("Saving does not revert live audio"), F.Settings->MasterVolume, .8f);
    TestEqual(TEXT("Saving does not revert live display"), F.Settings->GetScreenResolution(), FIntPoint(1600, 900));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOptionsCommitReloadTest, "ProjectRPG.Options.Commit.SuccessfulCommitAndReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOptionsCommitReloadTest::RunTest(const FString&)
{
    using namespace RPGOptionsCommitTests;
    if (!IsIsolated(*this)) return true;
    FFixture F;
    F.Settings->MasterVolume = .7f;
    F.Settings->SetScreenResolution(FIntPoint(1600, 900));
    TestTrue(TEXT("Explicit sound commit"), F.Settings->CommitSoundOptions(F.Settings->GetSoundOptions()));
    TestTrue(TEXT("Explicit graphics commit"), F.Settings->CommitGraphicsOptions());
    F.Settings->SaveSettings();
    const FConfigFile Disk = F.ReadDisk();
    float Value = 0.f; int32 Width = 0;
    Disk.GetFloat(FFixture::Section, TEXT("MasterVolume"), Value);
    Disk.GetInt(FFixture::Section, TEXT("ResolutionSizeX"), Width);
    TestEqual(TEXT("Committed sound mirrored to disk"), Value, .7f);
    TestEqual(TEXT("Committed display mirrored to disk"), Width, 1600);
    F.Settings->MasterVolume = .2f;
    F.Settings->LoadSettings(false);
    TestEqual(TEXT("Legacy INI fallback reloads saved value"), F.Settings->MasterVolume, .7f);
    F.Settings->MasterVolume = .9f;
    F.Settings->SaveSettings();
    const FConfigFile Reloaded = F.ReadDisk();
    Reloaded.GetFloat(FFixture::Section, TEXT("MasterVolume"), Value);
    TestEqual(TEXT("Reload established a committed baseline"), Value, .7f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGOptionsCallbackTest, "ProjectRPG.Options.Commit.SaveCallbackCannotCommitPreview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGOptionsCallbackTest::RunTest(const FString&)
{
    using namespace RPGOptionsCommitTests;
    if (!IsIsolated(*this)) return true;
    FFixture F;
    auto Previous = F.Settings->OnUpdateCloudDataFromGameUserSettings;
    bool bCalled = false;
    F.Settings->OnUpdateCloudDataFromGameUserSettings.BindLambda([&](const FString&)
    {
        bCalled = true;
        TestFalse(TEXT("Sound apply blocked during temporary INI masking"), F.Settings->CanApplySoundOptions());
        TestFalse(TEXT("Nested commit rejected"), F.Settings->CommitSoundOptions(FRPGSoundSettings()));
        TestEqual(TEXT("Engine saves the masked audio snapshot"), F.Settings->MasterVolume, .4f);
        F.Settings->SaveSettings();
        return true;
    });
    F.Settings->MasterVolume = .9f;
    F.Settings->SaveSettings();
    F.Settings->OnUpdateCloudDataFromGameUserSettings = Previous;
    TestTrue(TEXT("Engine save callback executed"), bCalled);
    TestEqual(TEXT("Live audio restored after callback"), F.Settings->MasterVolume, .9f);
    return true;
}
#endif
