#include "Riding/RPGRidingIntentJournal.h"
#include "Riding/RPGRidingIntentJson.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "String/BytesToHex.h"
#include "Policies/CondensedJsonPrintPolicy.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
    constexpr int64 MaximumBytes = 65536;
    FString Id(FGuid G) { return G.ToString(EGuidFormats::DigitsWithHyphensLower); }
    bool Hash(const FString& Text, FString& Out)
    {
        Out.Reset(); const FTCHARToUTF8 Bytes(*Text); uint8 Digest[SHA256_DIGEST_LENGTH];
        if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length(), Digest)) return false;
        Out = BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower(); return true;
    }
    bool Plain(const FString& Path)
    {
        auto& Platform = FPlatformFileManager::Get().GetPlatformFile(); FString Cursor = FPaths::ConvertRelativePathToFull(Path);
        while (!Cursor.IsEmpty())
        {
            if (Platform.IsSymlink(*Cursor) != ESymlinkResult::NonSymlink) return false;
            const auto Parent = FPaths::GetPath(Cursor); if (Parent == Cursor) break; Cursor = Parent;
        }
        return true;
    }
    struct FLockedPath
    {
        FString Path, Key; TUniquePtr<IFileHandle> Lock;
        ERPGRidingJournalRead Failure = ERPGRidingJournalRead::Unavailable;
        FLockedPath(const FString& Root, const FRPGRidingIntentScope& Scope)
        {
#if PLATFORM_WINDOWS
            if (Root.IsEmpty() || FPaths::IsRelative(Root) || !FRPGRidingIntentJournal::ScopeKey(Scope, Key)) return;
            FString Directory = FPaths::ConvertRelativePathToFull(Root); FPaths::NormalizeDirectoryName(Directory);
            auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
            if (Directory.Len() <= 3 || !Plain(Directory) || !Platform.CreateDirectoryTree(*Directory) || !Plain(Directory)) return;
            Path = Directory / (Key + TEXT(".json")); const auto LockPath = Directory / (Key + TEXT(".lock"));
            if (!Plain(Path) || !Plain(LockPath)) return;
            Lock.Reset(Platform.OpenWrite(*LockPath, false, false)); if (!Lock) Failure = ERPGRidingJournalRead::Busy;
#endif
        }
    };
    bool Fingerprint(const FString& Key, const FRPGRidingPendingIntent& I, FString& Out)
    { return I.IsValid() && Hash(TEXT("RidingIntent.v1\n") + Key + TEXT("\n") + Id(I.RequestId) + TEXT("\n") + I.Method() + TEXT("\n") + I.Path() + TEXT("\n") + I.Body, Out); }
    ERPGRidingJournalRead Read(const FString& Path, const FString& Key, const FRPGRidingIntentScope& Scope, FRPGRidingPendingIntent& Out)
    {
        using namespace RPGRidingIntentJson;
        Out = {}; auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
        if (!Plain(Path) || Platform.DirectoryExists(*Path)) return ERPGRidingJournalRead::Invalid;
        if (!Platform.FileExists(*Path)) return ERPGRidingJournalRead::Missing;
        const int64 Size = Platform.FileSize(*Path); if (Size < 1 || Size > MaximumBytes) return ERPGRidingJournalRead::Invalid;
        FString Text; if (!FFileHelper::LoadFileToString(Text, *Path) || !UniqueJson(Text)) return ERPGRidingJournalRead::Invalid;
        TSharedPtr<FJsonObject> O; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O) || !O || O->Values.Num() != 9) return ERPGRidingJournalRead::Invalid;
        FString Version, SavedKey, Character, Action, Method, SavedPath, SavedFingerprint; FRPGRidingPendingIntent I; I.Scope = Scope;
        if (!String(O, TEXT("version"), Version) || Version != TEXT("1") || !String(O, TEXT("scope"), SavedKey) || SavedKey != Key
            || !String(O, TEXT("characterId"), Character) || Character != Id(Scope.Character) || !Guid(O, TEXT("requestId"), I.RequestId)
            || !String(O, TEXT("operation"), Action) || !String(O, TEXT("method"), Method) || !String(O, TEXT("path"), SavedPath)
            || !String(O, TEXT("body"), I.Body) || !String(O, TEXT("fingerprint"), SavedFingerprint)) return ERPGRidingJournalRead::Invalid;
        if (Action == TEXT("register")) I.Operation = ERPGRidingPersistentOperation::Register;
        else if (Action == TEXT("preferences")) I.Operation = ERPGRidingPersistentOperation::Preferences;
        else return ERPGRidingJournalRead::Invalid;
        FString Expected;
        if (!I.IsValid() || Method != I.Method() || SavedPath != I.Path() || !Fingerprint(Key, I, Expected) || Expected != SavedFingerprint) return ERPGRidingJournalRead::Invalid;
        Out = MoveTemp(I); return ERPGRidingJournalRead::Ready;
    }
    bool Write(const FString& Path, const FString& Key, const FRPGRidingPendingIntent& I)
    {
        FString Digest; if (!Fingerprint(Key, I, Digest)) return false;
        auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("version"), TEXT("1")); O->SetStringField(TEXT("scope"), Key);
        O->SetStringField(TEXT("characterId"), Id(I.Scope.Character)); O->SetStringField(TEXT("requestId"), Id(I.RequestId));
        O->SetStringField(TEXT("operation"), I.Operation == ERPGRidingPersistentOperation::Register ? TEXT("register") : TEXT("preferences"));
        O->SetStringField(TEXT("method"), I.Method()); O->SetStringField(TEXT("path"), I.Path()); O->SetStringField(TEXT("body"), I.Body); O->SetStringField(TEXT("fingerprint"), Digest);
        FString Text; if (!FJsonSerializer::Serialize(O, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return false;
        const FTCHARToUTF8 Bytes(*Text); if (Bytes.Length() > MaximumBytes) return false;
        const auto Temporary = Path + TEXT(".tmp-") + Id(FGuid::NewGuid()); auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
        if (!Plain(Temporary) || Platform.FileExists(*Temporary)) return false;
        TUniquePtr<IFileHandle> Output(Platform.OpenWrite(*Temporary, false, false));
        const bool bWritten = Output && Output->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) && Output->Flush(true); Output.Reset();
        const bool bMoved = bWritten && Plain(Path) && IFileManager::Get().Move(*Path, *Temporary, false, false, false, true);
        if (!bMoved && Plain(Temporary)) Platform.DeleteFile(*Temporary); return bMoved;
    }
}
bool FRPGRidingIntentJournal::ScopeKey(const FRPGRidingIntentScope& Scope, FString& Out)
{ Out.Reset(); return Scope.IsValid() && Hash(TEXT("RidingIntentScope.v1\n") + Scope.Endpoint + TEXT("\n") + Scope.Account + TEXT("\n") + Id(Scope.Character), Out); }
ERPGRidingJournalRead FRPGRidingIntentJournal::Load(const FString& Root, const FRPGRidingIntentScope& Scope, FRPGRidingPendingIntent& Out)
{ Out = {}; FLockedPath Locked(Root, Scope); return Locked.Lock ? Read(Locked.Path, Locked.Key, Scope, Out) : Locked.Failure; }
bool FRPGRidingIntentJournal::Create(const FString& Root, const FRPGRidingPendingIntent& Intent)
{
    if (!Intent.IsValid()) return false; FLockedPath Locked(Root, Intent.Scope); FRPGRidingPendingIntent Existing;
    return Locked.Lock && Read(Locked.Path, Locked.Key, Intent.Scope, Existing) == ERPGRidingJournalRead::Missing && Write(Locked.Path, Locked.Key, Intent);
}
bool FRPGRidingIntentJournal::Clear(const FString& Root, const FRPGRidingPendingIntent& Expected)
{
    if (!Expected.IsValid()) return false; FLockedPath Locked(Root, Expected.Scope); FRPGRidingPendingIntent Existing;
    return Locked.Lock && Read(Locked.Path, Locked.Key, Expected.Scope, Existing) == ERPGRidingJournalRead::Ready && Existing == Expected
        && Plain(Locked.Path) && FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Locked.Path);
}
