#include "Item/Transaction/RPGPickupGrantJournal.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Transaction/RPGPickupGrantReceiptPolicy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr int32 MaximumEntries = 1024;
constexpr int64 MaximumEntryBytes = 64 * 1024;

bool IsPickupCommand(const FRPGItemRepositoryCommitRequest& Request)
{
	FGuid OwnerId;
	if (Request.Operation != FName(TEXT("Item.Pickup")) ||
		Request.Actor.Type != ERPGItemOwnerType::Character ||
		!FGuid::Parse(Request.Actor.OwnerId, OwnerId) || !OwnerId.IsValid() ||
		Request.Actor.OwnerId != OwnerId.ToString(EGuidFormats::DigitsWithHyphensLower) ||
		Request.Mutations.Num() != 1 || Request.AffectedQuantity <= 0) return false;
	const FRPGItemRecordMutation& Mutation = Request.Mutations[0];
	const FRPGItemRecord& Record = Mutation.NewRecord;
	return Mutation.ExpectedRevision == 0 && Record.GetRevision() == 0 &&
		Record.IsActive() && Record.GetOwner() == Request.Actor &&
		Record.GetQuantity() == Request.AffectedQuantity &&
		Record.GetLocation().ContainerType == ERPGItemContainerType::Inventory &&
		Record.GetLocation().ContainerId == Request.Actor.OwnerId;
}
}

FRPGPickupGrantJournal::FRPGPickupGrantJournal(FString InDirectory, FString InServerId, FString InSessionId)
	: Directory(FPaths::ConvertRelativePathToFull(InDirectory))
	, ServerId(MoveTemp(InServerId)), SessionId(MoveTemp(InSessionId))
{
}

FString FRPGPickupGrantJournal::PathFor(const FGuid& RequestId) const
{
	return FPaths::Combine(Directory, RequestId.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT(".json"));
}

bool FRPGPickupGrantJournal::ReadEntry(const FString& FileName,
	FRPGItemRepositoryCommitRequest& OutRequest, FString& OutBody, FGuid& OutDropId) const
{
	const FString Path = FPaths::Combine(Directory, FileName);
	const int64 Size = IFileManager::Get().FileSize(*Path);
	FString Contents;
	if (Size <= 0 || Size > MaximumEntryBytes || !FFileHelper::LoadFileToString(Contents, *Path)) return false;
	TSharedPtr<FJsonObject> Root;
	FString StoredServer, StoredSession;
	int32 Version = 0;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) || !Root ||
		!Root->TryGetNumberField(TEXT("schemaVersion"), Version) || (Version != 1 && Version != 2) ||
		!Root->TryGetStringField(TEXT("serverId"), StoredServer) || StoredServer != ServerId ||
		!Root->TryGetStringField(TEXT("sessionId"), StoredSession) || StoredSession != SessionId ||
		!Root->TryGetStringField(TEXT("body"), OutBody) ||
		!FRPGItemBackendJsonCodec::DeserializeCommitRequest(OutBody, OutRequest) ||
		!IsPickupCommand(OutRequest)) return false;
	OutDropId.Invalidate();
	FString DropText;
	if (Version == 2 && (!Root->TryGetStringField(TEXT("dropId"), DropText) ||
		!FGuid::Parse(DropText, OutDropId) || !OutDropId.IsValid() ||
		DropText != OutDropId.ToString(EGuidFormats::DigitsWithHyphensLower))) return false;
	// Never silently downgrade a v2 file by changing only its schema version.
	if (Version == 1 && Root->HasField(TEXT("dropId"))) return false;
	FString CanonicalBody;
	if (!FRPGItemBackendJsonCodec::SerializeCommitRequest(OutRequest, CanonicalBody) ||
		CanonicalBody != OutBody) return false;
	FString ExpectedName = FPaths::GetCleanFilename(PathFor(OutRequest.RequestId));
	return FileName == ExpectedName || FileName == ExpectedName + TEXT(".tmp");
}

bool FRPGPickupGrantJournal::Restore()
{
	bHealthy = false;
	Pending.Reset();
	FrozenBodies.Reset();
	DropIds.Reset();
	FGuid Session;
	if (ServerId.IsEmpty() || ServerId.Len() > 128 ||
		!FGuid::Parse(SessionId, Session) || !Session.IsValid() ||
		SessionId != Session.ToString(EGuidFormats::DigitsWithHyphensLower)) return false;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*")), true, false);
	if (Files.Num() > MaximumEntries * 2) return false;
	TMap<FGuid, FRPGItemRepositoryCommitRequest> Restored;
	TMap<FGuid, FString> Bodies;
	TMap<FGuid, FGuid> RestoredDrops;
	TMap<FGuid, FGuid> DropRequests;
	TArray<FString> TemporaryFiles;
	// Validate the whole directory before promoting/removing any file.
	for (const FString& File : Files)
	{
		FRPGItemRepositoryCommitRequest Request;
		FString Body;
		FGuid Drop;
		if (!ReadEntry(File, Request, Body, Drop)) return false;
		if (const FString* Existing = Bodies.Find(Request.RequestId); Existing && *Existing != Body) return false;
		if (const FGuid* Existing = RestoredDrops.Find(Request.RequestId); Existing && *Existing != Drop) return false;
		if (Drop.IsValid())
		{
			if (const FGuid* Existing = DropRequests.Find(Drop); Existing && *Existing != Request.RequestId) return false;
			DropRequests.Add(Drop, Request.RequestId);
		}
		RestoredDrops.Add(Request.RequestId, Drop);
		Bodies.Add(Request.RequestId, Body);
		Restored.Add(Request.RequestId, MoveTemp(Request));
		if (File.EndsWith(TEXT(".tmp"))) TemporaryFiles.Add(File);
	}
	if (Restored.Num() > MaximumEntries) return false;
	for (const FString& File : TemporaryFiles)
	{
		const FString From = FPaths::Combine(Directory, File);
		const FString To = From.LeftChop(4);
		if (!IFileManager::Get().Move(*To, *From, true, false, false, true)) return false;
	}
	Pending = MoveTemp(Restored);
	FrozenBodies = MoveTemp(Bodies);
	DropIds = MoveTemp(RestoredDrops);
	bHealthy = true;
	return true;
}

bool FRPGPickupGrantJournal::Persist(const FRPGItemRepositoryCommitRequest& Request, const FGuid& DropId)
{
	FString Body;
	if (!bHealthy || !IsPickupCommand(Request) ||
		!FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body)) return false;
	if (const FString* Existing = FrozenBodies.Find(Request.RequestId))
		return *Existing == Body && DropIds.FindRef(Request.RequestId) == DropId;
	if (DropId.IsValid())
		for (const auto& Entry : DropIds)
			if (Entry.Value == DropId && Entry.Key != Request.RequestId) return false;
	if (Pending.Num() >= MaximumEntries) return false;
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), DropId.IsValid() ? 2 : 1);
	if (DropId.IsValid()) Root->SetStringField(TEXT("dropId"), DropId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("serverId"), ServerId);
	Root->SetStringField(TEXT("sessionId"), SessionId);
	Root->SetStringField(TEXT("body"), Body);
	FString Contents;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Contents);
	if (!FJsonSerializer::Serialize(Root, Writer)) return false;
	const FTCHARToUTF8 Bytes(*Contents);
	if (Bytes.Length() > MaximumEntryBytes) return false;
	const FString FinalPath = PathFor(Request.RequestId);
	const FString TemporaryPath = FinalPath + TEXT(".tmp");
	// Never overwrite unvalidated data from another process/allocation.
	if (IFileManager::Get().FileExists(*FinalPath) || IFileManager::Get().FileExists(*TemporaryPath) ||
		!IFileManager::Get().MakeDirectory(*Directory, true)) { bHealthy = false; return false; }
	TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*TemporaryPath));
	if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) ||
		!Handle->Flush(true)) { bHealthy = false; return false; }
	Handle.Reset();
	if (!IFileManager::Get().Move(*FinalPath, *TemporaryPath, false, false, false, true))
	{
		bHealthy = false;
		return false;
	}
	Pending.Add(Request.RequestId, Request);
	FrozenBodies.Add(Request.RequestId, MoveTemp(Body));
	DropIds.Add(Request.RequestId, DropId);
	return true;
}

bool FRPGPickupGrantJournal::Resolve(const FRPGItemBackendCommitResult& Receipt)
{
	const FRPGItemRepositoryCommitRequest* Request = Pending.Find(Receipt.RequestId);
	if (!bHealthy || !Request || FRPGPickupGrantReceiptPolicy::Evaluate(*Request, Receipt,
		GetDropId(Receipt.RequestId).IsValid()) ==
		ERPGPickupGrantReceiptDecision::RetrySameCommit) return false;
	const FString Path = PathFor(Receipt.RequestId);
	// A failed cleanup retains the in-memory barrier and all remaining evidence.
	if (!IFileManager::Get().Delete(*(Path + TEXT(".tmp")), false, false, true) ||
		!IFileManager::Get().Delete(*Path, false, false, true)) { bHealthy = false; return false; }
	Pending.Remove(Receipt.RequestId);
	FrozenBodies.Remove(Receipt.RequestId);
	DropIds.Remove(Receipt.RequestId);
	return true;
}
