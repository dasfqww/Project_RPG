#include "Online/RPGDungeonDepartureJournal.h"

#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int64 MaximumBytes = 8192;
	bool CanonicalGuid(const FString& Value)
	{
		FGuid Guid;
		return FGuid::Parse(Value, Guid) && Guid.IsValid()
			&& Value.Equals(Guid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive);
	}
	bool ValidScope(const FString& Backend, const FString& Steam, const FString& Character)
	{
		if (Backend.IsEmpty() || Backend.Len() > 2048
			|| !(Backend.StartsWith(TEXT("http://")) || Backend.StartsWith(TEXT("https://")))
			|| Backend.Contains(TEXT("?")) || Backend.Contains(TEXT("#")) || Backend.Contains(TEXT("@"))
			|| Backend.Contains(TEXT("\\")) || !FRPGBackendPlayerAuthState::IsSteamId(Steam) || !CanonicalGuid(Character)) return false;
		for (TCHAR Char : Backend) if (FChar::IsWhitespace(Char) || FChar::IsControl(Char)) return false;
		return true;
	}
	FString Digest(const FString& Value)
	{
		const FTCHARToUTF8 Bytes(*Value);
		uint8 Hash[20];
		FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
	}
}

bool FRPGDungeonDepartureCommand::IsValid() const
{
	return ValidScope(BackendUrl, SteamId, CharacterId) && CanonicalGuid(SessionId) && CanonicalGuid(RequestId)
		&& LobbyRevision > 0 && LobbyRevision <= 9007199254740991LL;
}

bool FRPGDungeonDepartureCommand::SameAs(const FRPGDungeonDepartureCommand& Other) const
{
	return BackendUrl.Equals(Other.BackendUrl, ESearchCase::CaseSensitive) && SteamId == Other.SteamId && CharacterId == Other.CharacterId
		&& SessionId == Other.SessionId && RequestId == Other.RequestId && LobbyRevision == Other.LobbyRevision && bCancel == Other.bCancel;
}

FRPGDungeonDepartureJournal::FRPGDungeonDepartureJournal(const FString& InDirectory, const FString& InBackend,
	const FString& InSteam, const FString& InCharacter)
	: Directory(FPaths::ConvertRelativePathToFull(InDirectory)), BackendUrl(InBackend), SteamId(InSteam), CharacterId(InCharacter)
{
	const FString Key = Digest(BackendUrl + TEXT("\n") + SteamId + TEXT("\n") + CharacterId);
	Path = FPaths::Combine(Directory, Key + TEXT(".json"));
	LockName = TEXT("ProjectRPG-Departure-") + Digest(Path);
}

bool FRPGDungeonDepartureJournal::MatchesScope(const FRPGDungeonDepartureCommand& Command) const
{
	return Command.IsValid() && Command.BackendUrl.Equals(BackendUrl, ESearchCase::CaseSensitive)
		&& Command.SteamId == SteamId && Command.CharacterId == CharacterId;
}

bool FRPGDungeonDepartureJournal::Read(const FString& FilePath, FRPGDungeonDepartureCommand& Out) const
{
	const int64 Size = IFileManager::Get().FileSize(*FilePath);
	FString Contents, Revision;
	TSharedPtr<FJsonObject> Root;
	double Version = 0;
	Out = {};
	return Size > 0 && Size <= MaximumBytes && FFileHelper::LoadFileToString(Contents, *FilePath)
		&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) && Root && Root->Values.Num() == 8
		&& Root->TryGetNumberField(TEXT("schemaVersion"), Version) && Version == 1
		&& Root->TryGetStringField(TEXT("backendUrl"), Out.BackendUrl)
		&& Root->TryGetStringField(TEXT("steamId"), Out.SteamId)
		&& Root->TryGetStringField(TEXT("characterId"), Out.CharacterId)
		&& Root->TryGetStringField(TEXT("sessionId"), Out.SessionId)
		&& Root->TryGetStringField(TEXT("requestId"), Out.RequestId)
		&& Root->TryGetStringField(TEXT("lobbyRevision"), Revision) && Revision.Len() <= 16
		&& LexTryParseString(Out.LobbyRevision, *Revision) && LexToString(Out.LobbyRevision) == Revision
		&& Root->TryGetBoolField(TEXT("cancel"), Out.bCancel) && MatchesScope(Out);
}

ERPGDepartureJournalLoad FRPGDungeonDepartureJournal::LoadLocked(FRPGDungeonDepartureCommand& Out)
{
	Out = {};
	if (!ValidScope(BackendUrl, SteamId, CharacterId) || !IFileManager::Get().MakeDirectory(*Directory, true))
		return ERPGDepartureJournalLoad::Error;
	const FString Temporary = Path + TEXT(".tmp");
	if (IFileManager::Get().DirectoryExists(*Path) || IFileManager::Get().DirectoryExists(*Temporary))
		return ERPGDepartureJournalLoad::Error;
	const bool bFinal = IFileManager::Get().FileExists(*Path), bTemporary = IFileManager::Get().FileExists(*Temporary);
	FRPGDungeonDepartureCommand Final, Pending;
	if ((bFinal && !Read(Path, Final)) || (bTemporary && !Read(Temporary, Pending))
		|| (bFinal && bTemporary && !Final.SameAs(Pending))) return ERPGDepartureJournalLoad::Error;
	if (!bFinal && !bTemporary) return ERPGDepartureJournalLoad::Missing;
	if (bTemporary)
	{
		// Recover a completely flushed interrupted write; partial/conflicting files never become empty evidence.
		if (bFinal ? !IFileManager::Get().Delete(*Temporary, true, false, true)
			: !IFileManager::Get().Move(*Path, *Temporary, false, false, false, true)) return ERPGDepartureJournalLoad::Error;
	}
	Out = bFinal ? MoveTemp(Final) : MoveTemp(Pending);
	return ERPGDepartureJournalLoad::Found;
}

ERPGDepartureJournalLoad FRPGDungeonDepartureJournal::Load(FRPGDungeonDepartureCommand& Out)
{
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid()) { Out = {}; return ERPGDepartureJournalLoad::Error; }
	return LoadLocked(Out);
}

bool FRPGDungeonDepartureJournal::Persist(const FRPGDungeonDepartureCommand& Command)
{
	if (!MatchesScope(Command)) return false;
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid()) return false;
	FRPGDungeonDepartureCommand Existing;
	const auto Loaded = LoadLocked(Existing);
	if (Loaded == ERPGDepartureJournalLoad::Found) return Existing.SameAs(Command);
	if (Loaded != ERPGDepartureJournalLoad::Missing) return false;
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("backendUrl"), BackendUrl);
	Root->SetStringField(TEXT("steamId"), SteamId);
	Root->SetStringField(TEXT("characterId"), CharacterId);
	Root->SetStringField(TEXT("sessionId"), Command.SessionId);
	Root->SetStringField(TEXT("requestId"), Command.RequestId);
	Root->SetStringField(TEXT("lobbyRevision"), LexToString(Command.LobbyRevision));
	Root->SetBoolField(TEXT("cancel"), Command.bCancel);
	FString Contents;
	if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Contents))) return false;
	const FTCHARToUTF8 Bytes(*Contents);
	if (Bytes.Length() > MaximumBytes) return false;
	const FString Temporary = Path + TEXT(".tmp");
	TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Temporary));
	if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) || !Handle->Flush(true)) return false;
	Handle.Reset();
	return IFileManager::Get().Move(*Path, *Temporary, false, false, false, true);
}

bool FRPGDungeonDepartureJournal::Resolve(const FRPGDungeonDepartureCommand& Command)
{
	if (!MatchesScope(Command)) return false;
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid()) return false;
	FRPGDungeonDepartureCommand Existing;
	const auto Loaded = LoadLocked(Existing);
	if (Loaded == ERPGDepartureJournalLoad::Missing) return true;
	return Loaded == ERPGDepartureJournalLoad::Found && Existing.SameAs(Command)
		&& IFileManager::Get().Delete(*Path, true, false, true);
}
