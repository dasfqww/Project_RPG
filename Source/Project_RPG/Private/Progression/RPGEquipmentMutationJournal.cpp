#include "Progression/RPGEquipmentMutationJournal.h"

#include "Progression/RPGEquipmentMutationCodec.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGEquipmentJournal
{
	constexpr int64 MaximumFileBytes = 64 * 1024;
	bool ScopeValid(const FRPGEquipmentMutationScope& Scope)
	{
		if (!Scope.CharacterId.IsValid() || Scope.Key.Len() != 64) return false;
		for (const TCHAR C : Scope.Key) if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f'))) return false;
		return true;
	}
	bool Plain(const FString& Path)
	{
		auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
		FString Cursor = FPaths::ConvertRelativePathToFull(Path);
		while (!Cursor.IsEmpty())
		{
			if (Platform.IsSymlink(*Cursor) != ESymlinkResult::NonSymlink) return false;
			const FString Parent = FPaths::GetPath(Cursor);
			if (Parent == Cursor) break;
			Cursor = Parent;
		}
		return true;
	}
	struct FLockedPath
	{
		FString Path;
		TUniquePtr<IFileHandle> Lock;
		ERPGEquipmentJournalRead Failure = ERPGEquipmentJournalRead::Unavailable;
		FLockedPath(const FString& Root, const FRPGEquipmentMutationScope& Scope)
		{
#if PLATFORM_WINDOWS
			// OpenWrite(..., bAllowRead=false) denies sharing for other writers on Windows.
			// Other platforms need an equivalent verified cross-process lock first.
			if (!ScopeValid(Scope) || Root.IsEmpty() || FPaths::IsRelative(Root)) return;
			FString Directory = FPaths::ConvertRelativePathToFull(Root);
			FPaths::NormalizeDirectoryName(Directory);
			if (Directory.Len() <= 3 || !Plain(Directory)) return;
			auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
			if (!Platform.CreateDirectoryTree(*Directory) || !Plain(Directory)) return;
			Path = FPaths::Combine(Directory, Scope.Key + TEXT(".json"));
			const FString LockPath = FPaths::Combine(Directory, Scope.Key + TEXT(".lock"));
			if (!Plain(Path) || !Plain(LockPath)) return;
			Lock.Reset(Platform.OpenWrite(*LockPath, false, false));
			if (!Lock) Failure = ERPGEquipmentJournalRead::Busy;
#endif
		}
	};
	bool String(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, FString& Out)
	{
		const auto Value = Root->TryGetField(Key);
		return Value && Value->Type == EJson::String && Value->TryGetString(Out);
	}
	ERPGEquipmentJournalRead Read(const FString& Path, const FRPGEquipmentMutationScope& Scope, FRPGEquipmentPendingMutation& Out)
	{
		Out = {};
		auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
		if (!Plain(Path)) return ERPGEquipmentJournalRead::Invalid;
		if (Platform.DirectoryExists(*Path)) return ERPGEquipmentJournalRead::Invalid;
		if (!Platform.FileExists(*Path)) return ERPGEquipmentJournalRead::Missing;
		const int64 Size = Platform.FileSize(*Path);
		if (Size < 1 || Size > MaximumFileBytes) return ERPGEquipmentJournalRead::Invalid;
		FString Text; TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
			|| !Root || Root->Values.Num() != 6) return ERPGEquipmentJournalRead::Invalid;
		const auto Version = Root->TryGetField(TEXT("version")); double Number = 0;
		FString Key, Character, Route, Body, Fingerprint;
		if (!Version || Version->Type != EJson::Number || !Version->TryGetNumber(Number) || Number != 1
			|| !String(Root, TEXT("scope"), Key) || !Key.Equals(Scope.Key, ESearchCase::CaseSensitive)
			|| !String(Root, TEXT("characterId"), Character)
			|| !Character.Equals(Scope.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
			|| !String(Root, TEXT("operation"), Route) || !String(Root, TEXT("body"), Body) || !String(Root, TEXT("fingerprint"), Fingerprint)) return ERPGEquipmentJournalRead::Invalid;
		ERPGEquipmentMutationOperation Operation = ERPGEquipmentMutationOperation::Count;
		for (int32 Index = 0; Index < static_cast<int32>(ERPGEquipmentMutationOperation::Count); ++Index)
			if (Route.Equals(FRPGEquipmentMutationCodec::Route(static_cast<ERPGEquipmentMutationOperation>(Index)), ESearchCase::CaseSensitive)) Operation = static_cast<ERPGEquipmentMutationOperation>(Index);
		FRPGEquipmentMutationCommand Command; FString Canonical, ExpectedFingerprint;
		if (!FRPGEquipmentMutationCodec::Decode(Scope.CharacterId, Operation, Body, Command)
			|| !FRPGEquipmentMutationCodec::Encode(Command, Canonical, ExpectedFingerprint)
			|| !Fingerprint.Equals(ExpectedFingerprint, ESearchCase::CaseSensitive)) return ERPGEquipmentJournalRead::Invalid;
		Out.Scope = Scope; Out.Command = MoveTemp(Command); Out.Body = MoveTemp(Body); Out.Fingerprint = MoveTemp(Fingerprint);
		return ERPGEquipmentJournalRead::Ready;
	}
	bool Write(const FString& Path, const FRPGEquipmentMutationScope& Scope, const FRPGEquipmentMutationCommand& Command)
	{
		FString Body, Fingerprint;
		if (Command.CharacterId != Scope.CharacterId || !FRPGEquipmentMutationCodec::Encode(Command, Body, Fingerprint)) return false;
		auto Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("version"), 1);
		Root->SetStringField(TEXT("scope"), Scope.Key);
		Root->SetStringField(TEXT("characterId"), Scope.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower));
		Root->SetStringField(TEXT("operation"), FRPGEquipmentMutationCodec::Route(Command.Operation));
		Root->SetStringField(TEXT("body"), Body); Root->SetStringField(TEXT("fingerprint"), Fingerprint);
		FString Text;
		if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text))) return false;
		const FTCHARToUTF8 Bytes(*Text);
		if (Bytes.Length() > MaximumFileBytes) return false;
		const FString Temporary = Path + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		auto& Platform = FPlatformFileManager::Get().GetPlatformFile();
		if (!Plain(Temporary) || Platform.FileExists(*Temporary)) return false;
		TUniquePtr<IFileHandle> Output(Platform.OpenWrite(*Temporary, false, false));
		const bool Written = Output && Output->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) && Output->Flush(true);
		Output.Reset();
		// Same-directory move; never replace another pending intent.
		const bool Moved = Written && Plain(Path) && IFileManager::Get().Move(*Path, *Temporary, false, false, false, true);
		if (!Moved && Plain(Temporary)) Platform.DeleteFile(*Temporary);
		return Moved;
	}
}

ERPGEquipmentJournalRead FRPGEquipmentMutationJournal::Load(const FString& Root, const FRPGEquipmentMutationScope& Scope,
	FRPGEquipmentPendingMutation& OutPending)
{
	OutPending = {};
	RPGEquipmentJournal::FLockedPath Locked(Root, Scope);
	return Locked.Lock ? RPGEquipmentJournal::Read(Locked.Path, Scope, OutPending) : Locked.Failure;
}

bool FRPGEquipmentMutationJournal::Create(const FString& Root, const FRPGEquipmentMutationScope& Scope,
	const FRPGEquipmentMutationCommand& Command)
{
	RPGEquipmentJournal::FLockedPath Locked(Root, Scope); FRPGEquipmentPendingMutation Existing;
	return Locked.Lock && RPGEquipmentJournal::Read(Locked.Path, Scope, Existing) == ERPGEquipmentJournalRead::Missing
		&& RPGEquipmentJournal::Write(Locked.Path, Scope, Command);
}

bool FRPGEquipmentMutationJournal::Clear(const FString& Root, const FRPGEquipmentMutationScope& Scope,
	const FRPGEquipmentMutationCommand& Expected)
{
	RPGEquipmentJournal::FLockedPath Locked(Root, Scope); FRPGEquipmentPendingMutation Existing;
	FString Body, Fingerprint;
	if (!Locked.Lock || RPGEquipmentJournal::Read(Locked.Path, Scope, Existing) != ERPGEquipmentJournalRead::Ready
		|| Expected.CharacterId != Scope.CharacterId || !FRPGEquipmentMutationCodec::Encode(Expected, Body, Fingerprint)
		|| Existing.Command.RequestId != Expected.RequestId || !Existing.Body.Equals(Body, ESearchCase::CaseSensitive)
		|| !Existing.Fingerprint.Equals(Fingerprint, ESearchCase::CaseSensitive) || !RPGEquipmentJournal::Plain(Locked.Path)) return false;
	return FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Locked.Path);
}
