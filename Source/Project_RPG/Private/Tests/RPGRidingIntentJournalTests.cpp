#include "Riding/RPGRidingIntentJournal.h"
#include "Riding/RPGRidingIntentJson.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& Text)
    { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O); return O; }
    TSharedPtr<FJsonObject> Fixture()
    {
        FString Text; FFileHelper::LoadFileToString(Text, *FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE")));
        auto O = Object(Text); return O && O->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? O : nullptr;
    }
    FRPGRidingIntentScope Scope()
    {
        auto F = Fixture(); FRPGRidingIntentScope S; if (!F) return S;
        FGuid Character; FGuid::Parse(F->GetStringField(TEXT("character")), Character);
        FRPGRidingIntentScope::Create(F->GetStringField(TEXT("endpoint")), F->GetStringField(TEXT("account")), Character, S); return S;
    }
    FRPGRidingPendingIntent Registration()
    {
        FRPGRidingPendingIntent I; auto F = Fixture(); if (!F) return I; auto O = Object(F->GetStringField(TEXT("registrationBody")));
        FGuid Request, Item; FGuid::Parse(O->GetStringField(TEXT("requestId")), Request); FGuid::Parse(O->GetStringField(TEXT("itemId")), Item);
        RPGRidingCommand::Register(Scope(), Request, Item, FCString::Atoi64(*O->GetStringField(TEXT("expectedItemRevision"))), static_cast<int32>(O->GetNumberField(TEXT("catalogVersion"))), I); return I;
    }
    FRPGRidingPendingIntent Preferences()
    {
        FRPGRidingPendingIntent I; auto F = Fixture(); if (!F) return I; auto O = Object(F->GetStringField(TEXT("preferencesBody"))); FGuid Request;
        FGuid::Parse(O->GetStringField(TEXT("requestId")), Request); FRPGRidingPreferencesCommand C; C.SelectedMountId = TEXT("horse"); C.Favorites = {TEXT("horse")};
        C.Shortcut = {ERPGRidingSelectorKind::Specific, TEXT("horse")}; RPGRidingCommand::Preferences(Scope(), Request, C, I); return I;
    }
    FString Root(const TCHAR* Case)
    { FString Path; FParse::Value(FCommandLine::Get(), TEXT("RidingIntentRoot="), Path); return Path.IsEmpty() ? FString() : Path / Case; }
    FString File(const FString& Directory, const FRPGRidingIntentScope& S, const TCHAR* Extension = TEXT(".json"))
    { FString Key; FRPGRidingIntentJournal::ScopeKey(S, Key); return Directory / (Key + Extension); }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentCommands, "ProjectRPG.Riding.Intent.Commands", Flags)
bool FRidingIntentCommands::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) { AddError(TEXT("Real accepted backend request fixture missing")); return false; }
    const auto R = Registration(), P = Preferences(); TestTrue(TEXT("Valid immutable intents"), R.IsValid() && P.IsValid());
    TestEqual(TEXT("Registration matches actual accepted backend bytes"), R.Body, F->GetStringField(TEXT("registrationBody")));
    TestEqual(TEXT("Preferences matches actual accepted backend bytes"), P.Body, F->GetStringField(TEXT("preferencesBody")));
    TestEqual(TEXT("POST"), R.Method(), FString(TEXT("POST"))); TestEqual(TEXT("PUT"), P.Method(), FString(TEXT("PUT")));
    FRPGRidingPreferencesCommand C; C.Favorites = {TEXT("wolf"), TEXT("horse")}; C.Shortcut.Kind = ERPGRidingSelectorKind::RandomFavorites;
    FRPGRidingPendingIntent A, B; RPGRidingCommand::Preferences(Scope(), P.RequestId, C, A); C.Favorites = {TEXT("horse"), TEXT("wolf")}; RPGRidingCommand::Preferences(Scope(), P.RequestId, C, B);
    TestTrue(TEXT("Favorite order canonical, original bytes stable"), A == B && A.IsValid());
    FRPGRidingIntentScope Normal; TestTrue(TEXT("Normalized API base"), FRPGRidingIntentScope::Create(Scope().Endpoint + TEXT("///"), Scope().Account, Scope().Character, Normal) && Normal == Scope());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentReject, "ProjectRPG.Riding.Intent.CommandRejection", Flags)
bool FRidingIntentReject::RunTest(const FString&)
{
    auto I = Registration(); if (!I.IsValid()) return false; FRPGRidingPendingIntent Out = I;
    TestFalse(TEXT("Zero item revision"), RPGRidingCommand::Register(Scope(), I.RequestId, FGuid::NewGuid(), 0, 7, Out)); TestFalse(TEXT("Failure clears output"), Out.RequestId.IsValid());
    TestFalse(TEXT("Int64 sentinel"), RPGRidingCommand::Register(Scope(), I.RequestId, FGuid::NewGuid(), MAX_int64, 7, Out));
    FRPGRidingPreferencesCommand C; C.ExpectedRevision = -1; TestFalse(TEXT("Negative prefs revision"), RPGRidingCommand::Preferences(Scope(), I.RequestId, C, Out));
    C.ExpectedRevision = 0; C.Favorites = {TEXT("horse"), TEXT("horse")}; TestFalse(TEXT("Duplicate favorites"), RPGRidingCommand::Preferences(Scope(), I.RequestId, C, Out));
    C.Favorites.Reset(); C.Shortcut = {ERPGRidingSelectorKind::Specific, TEXT("")}; TestFalse(TEXT("Specific requires ID"), RPGRidingCommand::Preferences(Scope(), I.RequestId, C, Out));
    C.Shortcut = {ERPGRidingSelectorKind::Selected, TEXT("horse")}; TestFalse(TEXT("Non-specific cannot carry ID"), RPGRidingCommand::Preferences(Scope(), I.RequestId, C, Out));
    auto Bad = I; Bad.Body = I.Body.LeftChop(1) + TEXT(",\"catalogVersion\":7}"); TestFalse(TEXT("Duplicate JSON fields"), Bad.IsValid());
    Bad = I; Bad.RequestId = FGuid::NewGuid(); TestFalse(TEXT("UUID and body correlation"), Bad.IsValid());
    Bad = I; Bad.Operation = static_cast<ERPGRidingPersistentOperation>(255); TestFalse(TEXT("Unknown operation"), Bad.IsValid());
    Bad = I; Bad.Body += TEXT(" "); TestFalse(TEXT("Non-original body cannot be restored"), Bad.IsValid());
    auto S = Scope(); S.Endpoint += TEXT("?token=secret"); TestFalse(TEXT("Credential-bearing endpoint"), S.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentStorage, "ProjectRPG.Riding.Intent.Journal", Flags)
bool FRidingIntentStorage::RunTest(const FString&)
{
    const auto I = Registration(); const auto Directory = Root(TEXT("Storage")); if (!I.IsValid() || Directory.IsEmpty()) return false; FRPGRidingPendingIntent Loaded;
    TestEqual(TEXT("Missing"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Loaded), ERPGRidingJournalRead::Missing);
    TestTrue(TEXT("Persist before any dispatch"), FRPGRidingIntentJournal::Create(Directory, I)); TestFalse(TEXT("No overwrite even for identical request"), FRPGRidingIntentJournal::Create(Directory, I));
    TestEqual(TEXT("Exact load"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Loaded), ERPGRidingJournalRead::Ready); TestTrue(TEXT("Original bytes restored"), Loaded == I);
    auto Other = Preferences(); TestFalse(TEXT("Shared registration/preferences namespace"), FRPGRidingIntentJournal::Create(Directory, Other));
    TestFalse(TEXT("Different command cannot clear original"), FRPGRidingIntentJournal::Clear(Directory, Other));
    TestTrue(TEXT("Matching resolved intent clear"), FRPGRidingIntentJournal::Clear(Directory, I));
    TestEqual(TEXT("Cleared"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Loaded), ERPGRidingJournalRead::Missing);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentIsolation, "ProjectRPG.Riding.Intent.Isolation", Flags)
bool FRidingIntentIsolation::RunTest(const FString&)
{
    const auto I = Preferences(); const auto Directory = Root(TEXT("Isolation")); if (!I.IsValid()) return false; FRPGRidingPendingIntent Out;
    TestTrue(TEXT("Save preferences"), FRPGRidingIntentJournal::Create(Directory, I));
    for (int32 Index = 0; Index < 3; ++Index)
    { auto S = I.Scope; if (Index == 0) S.Character = FGuid::NewGuid(); if (Index == 1) S.Account = TEXT("76561700000000999"); if (Index == 2) S.Endpoint += TEXT("/other");
      TestEqual(TEXT("Foreign scope never restores"), FRPGRidingIntentJournal::Load(Directory, S, Out), ERPGRidingJournalRead::Missing); }
    FString Text; FFileHelper::LoadFileToString(Text, *File(Directory, I.Scope)); auto F = Fixture();
    TestFalse(TEXT("No bearer, account or endpoint in journal"), Text.Contains(F->GetStringField(TEXT("token"))) || Text.Contains(I.Scope.Account) || Text.Contains(I.Scope.Endpoint));
    TestTrue(TEXT("Scoped intent still available"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Out) == ERPGRidingJournalRead::Ready && Out == I);
    TestTrue(TEXT("Clear original"), FRPGRidingIntentJournal::Clear(Directory, I)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentFiles, "ProjectRPG.Riding.Intent.FileSafety", Flags)
bool FRidingIntentFiles::RunTest(const FString&)
{
    const auto I = Registration(); if (!I.IsValid()) return false; FRPGRidingPendingIntent Out;
    TestFalse(TEXT("Relative root rejected"), FRPGRidingIntentJournal::Create(TEXT("relative-root"), I));
    TestFalse(TEXT("Redirected root rejected"), FRPGRidingIntentJournal::Create(Root(TEXT("Redirect")), I));
    const auto Directory = Root(TEXT("Files")); FRPGRidingIntentJournal::Load(Directory, I.Scope, Out);
    auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
    TUniquePtr<IFileHandle> Held(Platform.OpenWrite(*File(Directory, I.Scope, TEXT(".lock")), false, false));
    TestTrue(TEXT("Own exclusive lock held"), Held.IsValid()); TestEqual(TEXT("Competing load busy"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Out), ERPGRidingJournalRead::Busy); Held.Reset();
    TestTrue(TEXT("Initial save"), FRPGRidingIntentJournal::Create(Directory, I)); FString Original; FFileHelper::LoadFileToString(Original, *File(Directory, I.Scope));
    for (const auto& Corrupt : {FString(TEXT("null")), Original.LeftChop(1) + TEXT(",\"version\":\"1\"}"), FString::ChrN(65537, TEXT('x'))})
    {
        FFileHelper::SaveStringToFile(Corrupt, *File(Directory, I.Scope), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        TestEqual(TEXT("Corruption rejected"), FRPGRidingIntentJournal::Load(Directory, I.Scope, Out), ERPGRidingJournalRead::Invalid);
        TestFalse(TEXT("Corruption cannot be overwritten or cleared"), FRPGRidingIntentJournal::Create(Directory, I) || FRPGRidingIntentJournal::Clear(Directory, I));
        FString After; FFileHelper::LoadFileToString(After, *File(Directory, I.Scope)); TestEqual(TEXT("Evidence preserved"), After, Corrupt);
    }
    // Fixture restores its own bytes only; product recovery never repairs or replaces a corrupt file.
    FFileHelper::SaveStringToFile(Original, *File(Directory, I.Scope), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Fixture cleanup"), FRPGRidingIntentJournal::Clear(Directory, I)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentWriter, "ProjectRPG.Riding.Intent.RestartWriter", Flags)
bool FRidingIntentWriter::RunTest(const FString&)
{ const auto I = Preferences(); return TestTrue(TEXT("Original pending survives process exit"), I.IsValid() && FRPGRidingIntentJournal::Create(Root(TEXT("Restart")), I)); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingIntentReader, "ProjectRPG.Riding.IntentRestart.Reader", Flags)
bool FRidingIntentReader::RunTest(const FString&)
{
    const auto I = Preferences(); FRPGRidingPendingIntent Loaded;
    TestEqual(TEXT("Separate process restores pending"), FRPGRidingIntentJournal::Load(Root(TEXT("Restart")), I.Scope, Loaded), ERPGRidingJournalRead::Ready);
    TestTrue(TEXT("Same UUID/body/path/method"), Loaded == I && Loaded.Path() == I.Path() && Loaded.Method() == I.Method());
    TestTrue(TEXT("No duplicate command before resolution"), !FRPGRidingIntentJournal::Create(Root(TEXT("Restart")), I));
    TestTrue(TEXT("Matching completion cleanup"), FRPGRidingIntentJournal::Clear(Root(TEXT("Restart")), I)); return true;
}
#endif
