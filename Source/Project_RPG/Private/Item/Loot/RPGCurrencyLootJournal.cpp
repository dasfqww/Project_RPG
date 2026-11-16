#include "Item/Loot/RPGCurrencyLootJournal.h"
#include "Economy/Backend/RPGEconomyBackendJsonCodec.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
namespace
{
    bool PlainLootPath(const FString& Path)
    {
        auto& Files = FPlatformFileManager::Get().GetPlatformFile();
        FString Cursor = FPaths::ConvertRelativePathToFull(Path);
        while (!Cursor.IsEmpty())
        {
            if (Files.IsSymlink(*Cursor) != ESymlinkResult::NonSymlink) return false;
            const FString Parent = FPaths::GetPath(Cursor);
            if (Parent == Cursor) break; Cursor = Parent;
        }
        return true;
    }
}
FRPGCurrencyLootJournal::FRPGCurrencyLootJournal() = default;
FRPGCurrencyLootJournal::~FRPGCurrencyLootJournal() = default;
bool FRPGCurrencyLootJournal::Create(const FString& Root, const FRPGEconomyTransactionRequest& Request)
{
    if (Lock || !FRPGEconomyBackendJsonCodec::SerializeTransactionRequest(Request, Body)) return false;
#if PLATFORM_WINDOWS
    FGuid Session;
    if (!FGuid::Parse(Request.DungeonSessionId, Session) || !Session.IsValid()) return false;
    const FString Directory = FPaths::ConvertRelativePathToFull(Root) / Session.ToString(EGuidFormats::Digits);
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlainLootPath(Directory) || !Files.CreateDirectoryTree(*Directory) || !PlainLootPath(Directory)) return false;
    Path = Directory / (Request.RequestId.ToString(EGuidFormats::Digits) + TEXT(".json"));
    if (!PlainLootPath(Path) || !PlainLootPath(Path + TEXT(".lock"))) return false;
    Lock.Reset(Files.OpenWrite(*(Path + TEXT(".lock")), false, false));
    if (!Lock) return false;
    // An earlier actor/process may have sent this ID already. Never replace or adopt it.
    if (Files.FileExists(*Path) || Files.DirectoryExists(*Path)) { Lock.Reset(); return false; }
    const FString Temp = Path + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!PlainLootPath(Temp)) { Lock.Reset(); return false; }
    const FTCHARToUTF8 Utf8(*Body);
    TUniquePtr<IFileHandle> Output(Files.OpenWrite(*Temp, false, false));
    const bool bWritten = Output && Output->Write(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()) && Output->Flush(true);
    Output.Reset();
    const bool bMoved = bWritten && IFileManager::Get().Move(*Path, *Temp, false, false, false, true);
    if (!bMoved) { Files.DeleteFile(*Temp); Lock.Reset(); }
    return bMoved;
#else
    return false;
#endif
}
bool FRPGCurrencyLootJournal::Clear()
{
    if (!Lock || !PlainLootPath(Path)) return false;
    FString Stored;
    auto& Files = FPlatformFileManager::Get().GetPlatformFile();
    if (Files.FileSize(*Path) > 65536 || !FFileHelper::LoadFileToString(Stored, *Path) || Stored != Body) return false;
    return Files.DeleteFile(*Path);
}
