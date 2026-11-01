#include "RPGOptionsPersistence.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Type/RPGStructTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGOptionsPersistence, Log, All);

namespace
{
    void CanonicalizeField(const TSharedRef<FJsonObject>& Object, const TCHAR* PropertyName)
    {
        if (Object->HasField(PropertyName)) { return; }
        const FString SerializedName = FJsonObjectConverter::StandardizeCase(FString(PropertyName));
        const TSharedPtr<FJsonValue> Value = Object->TryGetField(SerializedName);
        if (Value.IsValid())
        {
            Object->SetField(PropertyName, Value);
            Object->RemoveField(SerializedName);
        }
    }

    template <typename T>
    bool LoadOptionsFile(const FString& FullPath, T& OutData)
    {
        FString Json;
        if (!FFileHelper::LoadFileToString(Json, *FullPath)) { return false; }
        TSharedPtr<FJsonObject> Object;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object.IsValid())
        {
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Invalid options JSON: %s"), *FullPath);
            return false;
        }
        // Support authored names and the camel-case spelling produced by the existing exporter.
        for (const TCHAR* PropertyName : { TEXT("Resolution"), TEXT("WindowMode"), TEXT("bVSync"),
            TEXT("MasterVolume"), TEXT("bMasterMuted"), TEXT("Volumes"), TEXT("Mutes") })
        {
            CanonicalizeField(Object.ToSharedRef(), PropertyName);
        }
        T Loaded;
        if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), &Loaded))
        {
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Invalid options fields: %s"), *FullPath);
            return false;
        }
        OutData = MoveTemp(Loaded);
        return true;
    }

    template <typename T>
    bool LoadOptions(const TCHAR* FileName, T& OutData)
    {
        const FString FullPath = FPaths::ProjectSavedDir() / FileName;
        if (LoadOptionsFile(FullPath, OutData)) { return true; }
        if (LoadOptionsFile(FullPath + TEXT(".bak"), OutData))
        {
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Recovered options from backup: %s"), FileName);
            return true;
        }
        return false;
    }

    template <typename T>
    bool SaveOptions(const TCHAR* FileName, const T& Data, FText& OutError)
    {
        OutError = FText::GetEmpty();
        FString Json;
        if (!FJsonObjectConverter::UStructToJsonObjectString(Data, Json))
        {
            OutError = NSLOCTEXT("RPGOptionsPersistence", "SerializeFailed", "설정을 저장하지 못했습니다. 다시 시도해 주세요.");
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Could not serialize options: %s"), FileName);
            return false;
        }
        const FString Directory = FPaths::ProjectSavedDir();
        IFileManager& Files = IFileManager::Get();
        if (!Files.DirectoryExists(*Directory) && !Files.MakeDirectory(*Directory, true))
        {
            OutError = NSLOCTEXT("RPGOptionsPersistence", "DirectoryFailed", "설정을 저장할 폴더에 접근할 수 없습니다.");
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Could not create options directory: %s"), *Directory);
            return false;
        }
        const FString FullPath = Directory / FileName;
        const FString BackupPath = FullPath + TEXT(".bak");
        const FString Prefix = FString(FileName) + TEXT(".");
        const FString TemporaryPath = FPaths::CreateTempFilename(*Directory, *Prefix, TEXT(".tmp"));
        const auto ReportWriteFailure = [&](const TCHAR* Stage)
        {
            OutError = NSLOCTEXT("RPGOptionsPersistence", "WriteFailed", "설정 파일을 저장하지 못했습니다. 다시 시도해 주세요.");
            UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Could not save options Stage=%s File=%s"), Stage, *FullPath);
            return false;
        };

        // Exclusive creation establishes ownership before cleanup is registered.
        TUniquePtr<FArchive> Writer(Files.CreateFileWriter(*TemporaryPath, FILEWRITE_NoReplaceExisting));
        if (!Writer) { return ReportWriteFailure(TEXT("CreateTemporary")); }
        ON_SCOPE_EXIT
        {
            Writer.Reset();
            if (Files.FileExists(*TemporaryPath) && !Files.Delete(*TemporaryPath, false, false, true))
            {
                UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Could not remove temporary options file: %s"), *TemporaryPath);
            }
        };
        const FTCHARToUTF8 Encoded(*Json);
        TArray<uint8> Bytes;
        Bytes.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
        Writer->Serialize(Bytes.GetData(), Bytes.Num());
        const bool bWritten = Writer->Close() && !Writer->IsError() && !Writer->IsCriticalError();
        Writer.Reset();
        if (!bWritten) { return ReportWriteFailure(TEXT("WriteTemporary")); }

        // Never replace a usable backup with a primary that no longer parses.
        T Previous;
        if (Files.FileExists(*FullPath) && LoadOptionsFile(FullPath, Previous))
        {
            if (Files.Copy(*BackupPath, *FullPath, true, false, false) != COPY_OK)
            {
                return ReportWriteFailure(TEXT("BackupPrevious"));
            }
        }
        // The engine may delete the destination before moving; parseable previous versions were retained above.
        if (!Files.Move(*FullPath, *TemporaryPath, true, false, false, true))
        {
            T Recovery;
            if (LoadOptionsFile(BackupPath, Recovery))
            {
                const bool bRestored = Files.Copy(*FullPath, *BackupPath, true, false, false) == COPY_OK;
                UE_LOG(LogRPGOptionsPersistence, Warning, TEXT("Options backup restore Result=%d File=%s"), bRestored, *FullPath);
            }
            return ReportWriteFailure(TEXT("ReplacePrimary"));
        }
        return true;
    }
}

bool RPGOptionsPersistence::LoadGraphics(FGraphicSaveData& OutData)
{
    return LoadOptions(TEXT("graphic_options.json"), OutData);
}

bool RPGOptionsPersistence::LoadSound(FSoundSaveData& OutData)
{
    return LoadOptions(TEXT("sound_options.json"), OutData);
}

bool RPGOptionsPersistence::SaveGraphics(const FGraphicSaveData& Data, FText& OutError)
{
    return SaveOptions(TEXT("graphic_options.json"), Data, OutError);
}

bool RPGOptionsPersistence::SaveSound(const FSoundSaveData& Data, FText& OutError)
{
    return SaveOptions(TEXT("sound_options.json"), Data, OutError);
}
