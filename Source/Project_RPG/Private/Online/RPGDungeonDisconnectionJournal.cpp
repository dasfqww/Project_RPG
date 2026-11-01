#include "Online/RPGDungeonDisconnectionJournal.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/RPGJoinTicketAdmission.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 MaximumEntries = 256;
	constexpr int64 MaximumEntryBytes = 8192;
}

FString FRPGDungeonDisconnectionRecord::Key() const
{
	return CharacterId + TEXT("-") + LexToString(ConnectionGeneration);
}

bool FRPGDungeonDisconnectionRecord::SameAs(const FRPGDungeonDisconnectionRecord& Other) const
{
	return CharacterId.Equals(Other.CharacterId, ESearchCase::CaseSensitive)
		&& SteamId.Equals(Other.SteamId, ESearchCase::CaseSensitive) && ConnectionGeneration == Other.ConnectionGeneration
		&& ObservedAtUtc.ToIso8601().Equals(Other.ObservedAtUtc.ToIso8601(), ESearchCase::CaseSensitive)
		&& Body.Equals(Other.Body, ESearchCase::CaseSensitive);
}

bool FRPGDungeonDisconnectionRecord::EncodeBody(const FString& SessionId, FString& OutBody) const
{
	return ObservedAtUtc.GetTicks() == 0
		? FRPGDungeonConnectionRequest::Encode(SessionId, CharacterId, SteamId, ConnectionGeneration, OutBody)
		: FRPGDungeonConnectionRequest::EncodeDisconnection(SessionId, CharacterId, SteamId,
			ConnectionGeneration, ObservedAtUtc, OutBody);
}

FRPGDungeonDisconnectionJournal::FRPGDungeonDisconnectionJournal(FString InDirectory,
	FString InBackendUrl, FString InServerId, FString InSessionId)
	: Directory(FPaths::ConvertRelativePathToFull(InDirectory))
	, BackendUrl(MoveTemp(InBackendUrl)), ServerId(MoveTemp(InServerId)), SessionId(MoveTemp(InSessionId))
{
}

FString FRPGDungeonDisconnectionJournal::PathFor(const FString& Key) const
{
	return FPaths::Combine(Directory, Key + TEXT(".json"));
}

bool FRPGDungeonDisconnectionJournal::Validate(const FRPGDungeonDisconnectionRecord& Record) const
{
	FString Expected;
	return Record.EncodeBody(SessionId, Expected)
		&& Expected.Equals(Record.Body, ESearchCase::CaseSensitive);
}

bool FRPGDungeonDisconnectionJournal::Read(const FString& FileName,
	FRPGDungeonDisconnectionRecord& OutRecord) const
{
	const FString Path = FPaths::Combine(Directory, FileName);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	FString Contents;
	TSharedPtr<FJsonObject> Root;
	FString StoredBackend, StoredServer, StoredSession, Generation;
	double Version = 0;
	if (Size <= 0 || Size > MaximumEntryBytes || !FFileHelper::LoadFileToString(Contents, *Path)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) || !Root
		|| !Root->TryGetNumberField(TEXT("schemaVersion"), Version) || (Version != 1 && Version != 2)
		|| !Root->TryGetStringField(TEXT("backendUrl"), StoredBackend) || !StoredBackend.Equals(BackendUrl, ESearchCase::CaseSensitive)
		|| !Root->TryGetStringField(TEXT("serverId"), StoredServer) || !StoredServer.Equals(ServerId, ESearchCase::CaseSensitive)
		|| !Root->TryGetStringField(TEXT("sessionId"), StoredSession) || !StoredSession.Equals(SessionId, ESearchCase::CaseSensitive)
		|| !Root->TryGetStringField(TEXT("characterId"), OutRecord.CharacterId)
		|| !Root->TryGetStringField(TEXT("steamId"), OutRecord.SteamId)
		|| !Root->TryGetStringField(TEXT("connectionGeneration"), Generation)
		|| Generation.IsEmpty() || Generation.Len() > 19
		|| !LexTryParseString(OutRecord.ConnectionGeneration, *Generation)
		|| LexToString(OutRecord.ConnectionGeneration) != Generation
		|| !Root->TryGetStringField(TEXT("body"), OutRecord.Body)) return false;
	OutRecord.ObservedAtUtc = FDateTime();
	if (Version == 2)
	{
		FString Observation;
		if (!Root->TryGetStringField(TEXT("observedAtUtc"), Observation)
			|| !FDateTime::ParseIso8601(*Observation, OutRecord.ObservedAtUtc)
			|| OutRecord.ObservedAtUtc.GetTicks() <= 0
			|| !Observation.Equals(OutRecord.ObservedAtUtc.ToIso8601(), ESearchCase::CaseSensitive)) return false;
	}
	if (!Validate(OutRecord)) return false;
	const FString Expected = FPaths::GetCleanFilename(PathFor(OutRecord.Key()));
	return FileName.Equals(Expected, ESearchCase::CaseSensitive)
		|| FileName.Equals(Expected + TEXT(".tmp"), ESearchCase::CaseSensitive);
}

bool FRPGDungeonDisconnectionJournal::Restore()
{
	Pending.Reset();
	return RecoverForRetry();
}

bool FRPGDungeonDisconnectionJournal::MatchesAssignment(const FString& Backend, const FString& Server, const FString& Session) const
{
	return BackendUrl.Equals(Backend, ESearchCase::CaseSensitive) && ServerId.Equals(Server, ESearchCase::CaseSensitive)
		&& SessionId.Equals(Session, ESearchCase::CaseSensitive);
}

bool FRPGDungeonDisconnectionJournal::RecoverForRetry()
{
	bHealthy = false;
	FGuid Session;
	if (IFileManager::Get().FileExists(*Directory) || BackendUrl.IsEmpty() || BackendUrl.Len() > 2048 || ServerId.IsEmpty() || ServerId.Len() > 128
		|| !FGuid::Parse(SessionId, Session) || !Session.IsValid()
		|| !SessionId.Equals(Session.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)) return false;
	if (!IFileManager::Get().MakeDirectory(*Directory, true)) return false;
	TArray<FString> Files;
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	if (!Platform.IterateDirectory(*Directory, [&Files](const TCHAR* Path, bool bDirectory)
		{
			const FString Name = FPaths::GetCleanFilename(Path);
			// The consumption-observation journal is an independent, expected child.
			// Other directories (including a blocked report .tmp) are not empty evidence.
			if (!bDirectory || !Name.Equals(TEXT("ConsumeRecoveries"), ESearchCase::CaseSensitive)) Files.Add(Name);
			return Files.Num() <= MaximumEntries * 2;
		})) return false;
	if (Files.Num() > MaximumEntries * 2) return false;
	TMap<FString, FRPGDungeonDisconnectionRecord> Restored;
	TArray<FString> TemporaryFiles;
	for (const FString& File : Files)
	{
		FRPGDungeonDisconnectionRecord Record;
		if (!Read(File, Record)) return false;
		const FString Key = Record.Key();
		if (const auto* Existing = Restored.Find(Key); Existing && !Existing->SameAs(Record)) return false;
		Restored.Add(Key, MoveTemp(Record));
		if (File.EndsWith(TEXT(".tmp"))) TemporaryFiles.Add(File);
	}
	if (Restored.Num() > MaximumEntries) return false;
	for (const auto& Entry : Pending)
	{
		const auto* OnDisk = Restored.Find(Entry.Key);
		if (!OnDisk || !Entry.Value.SameAs(*OnDisk)) return false;
	}
	// Validate every entry before promoting any interrupted, fully flushed write.
	for (const FString& File : TemporaryFiles)
	{
		const FString From = FPaths::Combine(Directory, File);
		if (!IFileManager::Get().Move(*From.LeftChop(4), *From, true, false, false, true)) return false;
	}
	Pending = MoveTemp(Restored);
	bHealthy = true;
	return true;
}

bool FRPGDungeonDisconnectionJournal::Persist(const FRPGDungeonDisconnectionRecord& Record)
{
	if (!bHealthy || !Validate(Record)) return false;
	const FString Key = Record.Key();
	if (const auto* Existing = Pending.Find(Key)) return Existing->SameAs(Record);
	if (Pending.Num() >= MaximumEntries) return false;
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), Record.ObservedAtUtc.GetTicks() == 0 ? 1 : 2);
	Root->SetStringField(TEXT("backendUrl"), BackendUrl);
	Root->SetStringField(TEXT("serverId"), ServerId);
	Root->SetStringField(TEXT("sessionId"), SessionId);
	Root->SetStringField(TEXT("characterId"), Record.CharacterId);
	Root->SetStringField(TEXT("steamId"), Record.SteamId);
	Root->SetStringField(TEXT("connectionGeneration"), LexToString(Record.ConnectionGeneration));
	if (Record.ObservedAtUtc.GetTicks() > 0)
		Root->SetStringField(TEXT("observedAtUtc"), Record.ObservedAtUtc.ToIso8601());
	Root->SetStringField(TEXT("body"), Record.Body);
	FString Contents;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Contents);
	if (!FJsonSerializer::Serialize(Root, Writer)) return false;
	const FTCHARToUTF8 Bytes(*Contents);
	if (Bytes.Length() > MaximumEntryBytes) return false;
	const FString FinalPath = PathFor(Key);
	const FString TemporaryPath = FinalPath + TEXT(".tmp");
	if (IFileManager::Get().FileExists(*FinalPath) || IFileManager::Get().FileExists(*TemporaryPath)
		|| !IFileManager::Get().MakeDirectory(*Directory, true)) { bHealthy = false; return false; }
	TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*TemporaryPath));
	if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length())
		|| !Handle->Flush(true)) { bHealthy = false; return false; }
	Handle.Reset();
	if (!IFileManager::Get().Move(*FinalPath, *TemporaryPath, false, false, false, true))
	{ bHealthy = false; return false; }
	Pending.Add(Key, Record);
	return true;
}

bool FRPGDungeonDisconnectionJournal::Resolve(const FString& Key)
{
	if (!bHealthy || !Pending.Contains(Key)) return false;
	const FString Path = PathFor(Key);
	if (!IFileManager::Get().Delete(*(Path + TEXT(".tmp")), false, false, true)
		|| !IFileManager::Get().Delete(*Path, true, false, true)) return false;
	Pending.Remove(Key);
	return true;
}

bool FRPGDungeonDisconnectionJournal::IsAcceptedResponse(const FString& Json, int32 HttpStatus)
{
	if (HttpStatus != 200 || Json.IsEmpty() || Json.Len() > 4096) return false;
	TSharedPtr<FJsonObject> Root;
	FString Timestamp;
	FDateTime Parsed;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root
		&& Root->TryGetStringField(TEXT("disconnectedAt"), Timestamp)
		&& (Timestamp.EndsWith(TEXT("Z")) || Timestamp.EndsWith(TEXT("+00:00")))
		&& FDateTime::ParseIso8601(*Timestamp, Parsed) && Parsed.GetTicks() > 0;
}

bool FRPGDungeonDisconnectionJournal::IsTerminalRejection(const FString& Json, int32 HttpStatus)
{
	// An expired credential/rate limit is recoverable; an obsolete member/generation is not.
	if (HttpStatus == 403) return true;
	if ((HttpStatus != 404 && HttpStatus != 409) || Json.IsEmpty() || Json.Len() > 4096) return false;
	TSharedPtr<FJsonObject> Root;
	FString Error;
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root
		&& Root->TryGetStringField(TEXT("error"), Error)
		&& Error.Equals(HttpStatus == 404 ? TEXT("dungeon_session_not_found")
			: TEXT("dungeon_disconnection_not_available"), ESearchCase::CaseSensitive);
}
