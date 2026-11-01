#include "Online/RPGDungeonConsumeJournal.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaximumEntries = 256;
	constexpr int64 MaximumBytes = 4096;
	bool CanonicalGuid(const FString& Text)
	{
		FGuid Guid;
		return FGuid::Parse(Text, Guid) && Guid.IsValid()
			&& Text.Equals(Guid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive);
	}
	bool StringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* Name, FString& Text)
	{
		const auto Value = Object->TryGetField(Name);
		return Value && Value->Type == EJson::String && Value->TryGetString(Text);
	}
}

bool FRPGDungeonConsumeLossRecord::SameAs(const FRPGDungeonConsumeLossRecord& Other) const
{
	return RequestId.Equals(Other.RequestId, ESearchCase::CaseSensitive)
		&& TicketHash.Equals(Other.TicketHash, ESearchCase::CaseSensitive)
		&& ExpectedSteamId.Equals(Other.ExpectedSteamId, ESearchCase::CaseSensitive)
		&& ObservedAtUtc.ToIso8601().Equals(Other.ObservedAtUtc.ToIso8601(), ESearchCase::CaseSensitive);
}

FRPGDungeonConsumeJournal::FRPGDungeonConsumeJournal(FString InDirectory, FString InBackendUrl,
	FString InServerId, FString InSessionId)
	: Directory(FPaths::ConvertRelativePathToFull(InDirectory)), BackendUrl(MoveTemp(InBackendUrl)),
	  ServerId(MoveTemp(InServerId)), SessionId(MoveTemp(InSessionId)) {}

FString FRPGDungeonConsumeJournal::PathFor(const FString& RequestId) const
{
	return FPaths::Combine(Directory, RequestId + TEXT(".json"));
}

bool FRPGDungeonConsumeJournal::Validate(const FRPGDungeonConsumeLossRecord& Record) const
{
	if (!CanonicalGuid(Record.RequestId) || Record.TicketHash.Len() != 64
		|| Record.ExpectedSteamId.IsEmpty() || Record.ExpectedSteamId.Len() > 20
		|| Record.ObservedAtUtc.GetTicks() <= 0) return false;
	for (const TCHAR Character : Record.TicketHash)
		if (!((Character >= TEXT('0') && Character <= TEXT('9'))
			|| (Character >= TEXT('A') && Character <= TEXT('F')))) return false;
	for (const TCHAR Character : Record.ExpectedSteamId)
		if (Character < TEXT('0') || Character > TEXT('9')) return false;
	return true;
}

bool FRPGDungeonConsumeJournal::Read(const FString& FileName, FRPGDungeonConsumeLossRecord& Record) const
{
	const FString Path = FPaths::Combine(Directory, FileName);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	FString Contents, StoredBackend, StoredServer, StoredSession, Observation;
	TSharedPtr<FJsonObject> Root;
	double Version = 0;
	if (Size <= 0 || Size > MaximumBytes || !FFileHelper::LoadFileToString(Contents, *Path)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) || !Root
		|| Root->Values.Num() != 8 || !Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1
		|| !StringField(Root, TEXT("backendUrl"), StoredBackend) || !StoredBackend.Equals(BackendUrl, ESearchCase::CaseSensitive)
		|| !StringField(Root, TEXT("serverId"), StoredServer) || !StoredServer.Equals(ServerId, ESearchCase::CaseSensitive)
		|| !StringField(Root, TEXT("sessionId"), StoredSession) || !StoredSession.Equals(SessionId, ESearchCase::CaseSensitive)
		|| !StringField(Root, TEXT("requestId"), Record.RequestId)
		|| !StringField(Root, TEXT("ticketHash"), Record.TicketHash)
		|| !StringField(Root, TEXT("expectedSteamId"), Record.ExpectedSteamId)
		|| !StringField(Root, TEXT("observedAtUtc"), Observation)
		|| !FDateTime::ParseIso8601(*Observation, Record.ObservedAtUtc)
		|| !Observation.Equals(Record.ObservedAtUtc.ToIso8601(), ESearchCase::CaseSensitive)
		|| !Validate(Record)) return false;
	const FString Expected = FPaths::GetCleanFilename(PathFor(Record.RequestId));
	return FileName.Equals(Expected, ESearchCase::CaseSensitive)
		|| FileName.Equals(Expected + TEXT(".tmp"), ESearchCase::CaseSensitive);
}

bool FRPGDungeonConsumeJournal::Restore()
{
	Pending.Reset();
	return RecoverForRetry();
}

bool FRPGDungeonConsumeJournal::MatchesAssignment(const FString& Backend, const FString& Server, const FString& Session) const
{
	return BackendUrl.Equals(Backend, ESearchCase::CaseSensitive) && ServerId.Equals(Server, ESearchCase::CaseSensitive)
		&& SessionId.Equals(Session, ESearchCase::CaseSensitive);
}

bool FRPGDungeonConsumeJournal::RecoverForRetry()
{
	bHealthy = false;
	if (IFileManager::Get().FileExists(*Directory)
		|| BackendUrl.IsEmpty() || BackendUrl.Len() > 2048 || ServerId.IsEmpty() || ServerId.Len() > 128
		|| !CanonicalGuid(SessionId)) return false;
	// Missing is a valid empty journal only if its storage location is reachable.
	// Inaccessible ancestors or a file in the directory chain are not proof of absence.
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
	TArray<FString> Files, TemporaryFiles;
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	if (Platform.DirectoryExists(*Directory) && !Platform.IterateDirectory(*Directory,
		[&Files](const TCHAR* Path, bool)
		{
			// Unexpected directories are invalid records too; never mistake them for an empty journal.
			Files.Add(FPaths::GetCleanFilename(Path));
			return Files.Num() <= MaximumEntries * 2;
		})) return false;
	if (Files.Num() > MaximumEntries * 2) return false;
	TMap<FString, FRPGDungeonConsumeLossRecord> Restored;
	for (const FString& File : Files)
	{
		FRPGDungeonConsumeLossRecord Record;
		if (!Read(File, Record)) return false;
		if (const auto* Existing = Restored.Find(Record.RequestId); Existing && !Existing->SameAs(Record)) return false;
		const FString Key = Record.RequestId;
		Restored.Add(Key, MoveTemp(Record));
		if (File.EndsWith(TEXT(".tmp"), ESearchCase::CaseSensitive)) TemporaryFiles.Add(File);
	}
	if (Restored.Num() > MaximumEntries) return false;
	// A reachable empty directory is NOT recovery if a previously durable
	// observation disappeared or changed. Check before promoting any temporary file.
	for (const auto& Entry : Pending)
	{
		const auto* OnDisk = Restored.Find(Entry.Key);
		if (!OnDisk || !Entry.Value.SameAs(*OnDisk)) return false;
	}
	// No partial application/promotion until ALL files and duplicate copies validate.
	for (const FString& File : TemporaryFiles)
	{
		const FString From = FPaths::Combine(Directory, File);
		if (!IFileManager::Get().Move(*From.LeftChop(4), *From, true, false, false, true)) return false;
	}
	Pending = MoveTemp(Restored);
	bHealthy = true;
	return true;
}

bool FRPGDungeonConsumeJournal::Persist(const FRPGDungeonConsumeLossRecord& Record)
{
	if (!bHealthy || !Validate(Record)) return false;
	if (const auto* Existing = Pending.Find(Record.RequestId)) return Existing->SameAs(Record);
	if (Pending.Num() >= MaximumEntries) return false;
	const auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("backendUrl"), BackendUrl);
	Root->SetStringField(TEXT("serverId"), ServerId);
	Root->SetStringField(TEXT("sessionId"), SessionId);
	Root->SetStringField(TEXT("requestId"), Record.RequestId);
	Root->SetStringField(TEXT("ticketHash"), Record.TicketHash);
	Root->SetStringField(TEXT("expectedSteamId"), Record.ExpectedSteamId);
	Root->SetStringField(TEXT("observedAtUtc"), Record.ObservedAtUtc.ToIso8601());
	FString Contents;
	if (!FJsonSerializer::Serialize(Root,
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Contents))) return false;
	const FTCHARToUTF8 Bytes(*Contents);
	if (Bytes.Length() > MaximumBytes) return false;
	const FString Path = PathFor(Record.RequestId), Temporary = Path + TEXT(".tmp");
	if (IFileManager::Get().FileExists(*Path) || IFileManager::Get().FileExists(*Temporary)
		|| !IFileManager::Get().MakeDirectory(*Directory, true)) { bHealthy = false; return false; }
	TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Temporary));
	if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length())
		|| !Handle->Flush(true)) { bHealthy = false; return false; }
	Handle.Reset();
	if (!IFileManager::Get().Move(*Path, *Temporary, false, false, false, true)) { bHealthy = false; return false; }
	Pending.Add(Record.RequestId, Record);
	return true;
}

bool FRPGDungeonConsumeJournal::Resolve(const FString& RequestId)
{
	if (!bHealthy || !Pending.Contains(RequestId)) return false;
	const FString Path = PathFor(RequestId);
	if (!IFileManager::Get().Delete(*(Path + TEXT(".tmp")), false, false, true)
		|| !IFileManager::Get().Delete(*Path, true, false, true)) return false;
	Pending.Remove(RequestId);
	return true;
}
