#include "Item/Transaction/RPGInventoryCommandJournal.h"

#include "Dom/JsonObject.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr int64 MaximumBytes = 2 * 1024 * 1024;
constexpr int32 MaximumCommands = 32;
bool CanonicalGuid(const FString& Value)
{
	FGuid Id;
	return FGuid::Parse(Value, Id) && Id.IsValid() && Value == Id.ToString(EGuidFormats::DigitsWithHyphensLower);
}
FString Digest(const FString& Value)
{
	const FTCHARToUTF8 Bytes(*Value);
	uint8 Hash[20];
	FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash); // path identity only, not an authenticity signature
	return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
}
}

bool FRPGInventoryJournalScope::IsValid() const
{
	if (BackendUrl.IsEmpty() || BackendUrl.Len() > 2048 ||
		!(BackendUrl.StartsWith(TEXT("http://")) || BackendUrl.StartsWith(TEXT("https://"))) ||
		BackendUrl.Contains(TEXT("@")) || BackendUrl.Contains(TEXT("?")) || BackendUrl.Contains(TEXT("#")) || BackendUrl.Contains(TEXT("\\")) ||
		ServerId.IsEmpty() || ServerId.Len() > 128 || !CanonicalGuid(SessionId) || !CanonicalGuid(CharacterId) || SteamId.Len() != 17) return false;
	for (TCHAR C : SteamId) if (C < TEXT('0') || C > TEXT('9')) return false;
	for (TCHAR C : BackendUrl) if (FChar::IsWhitespace(C) || FChar::IsControl(C)) return false;
	for (TCHAR C : ServerId) if (FChar::IsControl(C)) return false;
	return true;
}

bool FRPGInventoryCommandJournal::Supports(const FName Operation)
{
	return Operation == TEXT("Item.Move") || Operation == TEXT("Item.Swap") ||
		Operation == TEXT("Item.StackTransfer") || Operation == TEXT("Item.Split") ||
		Operation == TEXT("Item.ClaimMail") || Operation == TEXT("Item.Equip") ||
		Operation == TEXT("Item.Unequip");
}

FRPGInventoryCommandJournal::FRPGInventoryCommandJournal(const FString& RootDirectory, FRPGInventoryJournalScope InScope)
	: Directory(RootDirectory.IsEmpty() ? FString() : FPaths::ConvertRelativePathToFull(RootDirectory)), Scope(MoveTemp(InScope))
{
	const FString Key = Digest(Scope.BackendUrl + TEXT("\n") + Scope.ServerId + TEXT("\n") + Scope.SessionId + TEXT("\n") + Scope.CharacterId + TEXT("\n") + Scope.SteamId);
	Path = FPaths::Combine(Directory, Key + TEXT(".json"));
	LockName = TEXT("ProjectRPG-Inventory-") + Digest(Path);
}

bool FRPGInventoryCommandJournal::ValidRequest(const FRPGItemRepositoryCommitRequest& Request) const
{
	// Swap counts no transferred units in the existing planner/wire contract.
	// Preserve that zero rather than changing the frozen idempotent request.
	const bool bSwap = Request.Operation == TEXT("Item.Swap");
	const bool bClaimMail = Request.Operation == TEXT("Item.ClaimMail");
	const bool bEquip = Request.Operation == TEXT("Item.Equip");
	const bool bEquipment = bEquip || Request.Operation == TEXT("Item.Unequip");
	const int32 ExpectedMutationCount = (Request.Operation == TEXT("Item.Move") || bClaimMail || bEquipment) ? 1 : 2;
	if (!Request.RequestId.IsValid() || !Supports(Request.Operation) || Request.Actor.Type != ERPGItemOwnerType::Character ||
		Request.Actor.OwnerId != Scope.CharacterId || (bSwap ? Request.AffectedQuantity != 0 : Request.AffectedQuantity <= 0) || Request.CommandFingerprint.IsEmpty() ||
		Request.CommandFingerprint.Len() > 4096 || Request.Mutations.Num() != ExpectedMutationCount) return false;
	if (bEquipment && Request.AffectedQuantity != 1) return false;
	TSet<FGuid> Ids;
	int32 Creates = 0;
	for (const auto& Mutation : Request.Mutations)
	{
		const auto& Record = Mutation.NewRecord;
		if (Mutation.ExpectedRevision < 0 || Mutation.ExpectedRevision == MAX_int64 || Record.GetRevision() != Mutation.ExpectedRevision ||
			Record.GetOwner() != Request.Actor || !Record.GetItemId().IsValid() || Ids.Contains(Record.GetItemId())) return false;
		if (Mutation.ExpectedRevision == 0) ++Creates;
		// A claim moves the whole existing attachment; it never grants a new identity or partial stack.
		if (bClaimMail && Record.GetQuantity() != Request.AffectedQuantity) return false;
		// Equipment recovery restores a state, never a one-time use effect or a new grant.
		if (bEquipment && (!Record.IsActive() || Record.GetQuantity() != 1 || Record.GetMetadata().bLocked)) return false;
		const int32 SlotLimit = bEquip ? static_cast<int32>(EEquipmentSlotType::Count) : RPGItemInventoryPolicy::SlotCapacity;
		const auto Container = bEquip ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory;
		// A fully merged source can be terminal; every active destination remains in the owner's container.
		if (Record.IsActive() && (Record.GetLocation().ContainerType != Container ||
			Record.GetLocation().ContainerId != Scope.CharacterId || !Record.GetLocation().IsActiveLocation() ||
			Record.GetLocation().SlotIndex >= SlotLimit)) return false;
		if (!Record.IsActive() && (Request.Operation != TEXT("Item.StackTransfer") || Record.GetLifecycleState() != ERPGItemLifecycleState::Consumed)) return false;
		Ids.Add(Record.GetItemId());
	}
	if (Request.Operation == TEXT("Item.Split"))
	{
		const auto* Created = Request.Mutations.FindByPredicate([](const auto& M) { return M.ExpectedRevision == 0; });
		const auto* Source = Request.Mutations.FindByPredicate([](const auto& M) { return M.ExpectedRevision > 0; });
		if (!Created || !Source || Created->NewRecord.GetQuantity() != Request.AffectedQuantity ||
			Created->NewRecord.GetItemId() != FRPGItemActionCommandPlanner::MakeSplitItemId(Request.RequestId, Source->NewRecord.GetItemId())) return false;
	}
	return Creates == (Request.Operation == TEXT("Item.Split") ? 1 : 0);
}

bool FRPGInventoryCommandJournal::Read(const FString& FilePath, FState& Out) const
{
	Out = {};
	FString Contents, Backend, Server, Session, Character, Steam, Generation;
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
	double Version = 0;
	const int64 Size = IFileManager::Get().FileSize(*FilePath);
	if (Size <= 0 || Size > MaximumBytes || !FFileHelper::LoadFileToString(Contents, *FilePath) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) || !Root || Root->Values.Num() != 9 ||
		!Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1 ||
		!Root->TryGetStringField(TEXT("backendUrl"), Backend) || Backend != Scope.BackendUrl ||
		!Root->TryGetStringField(TEXT("serverId"), Server) || Server != Scope.ServerId ||
		!Root->TryGetStringField(TEXT("sessionId"), Session) || Session != Scope.SessionId ||
		!Root->TryGetStringField(TEXT("characterId"), Character) || Character != Scope.CharacterId ||
		!Root->TryGetStringField(TEXT("steamId"), Steam) || Steam != Scope.SteamId ||
		!Root->TryGetStringField(TEXT("generation"), Generation) || Generation.Len() > 20 ||
		!LexTryParseString(Out.Generation, *Generation) || Out.Generation == 0 || LexToString(Out.Generation) != Generation ||
		!Root->TryGetBoolField(TEXT("revoked"), Out.bRevoked) || !Root->TryGetArrayField(TEXT("commands"), Commands) ||
		!Commands || Commands->Num() > MaximumCommands) return false;
	for (const auto& Value : *Commands)
	{
		FString Body, Canonical;
		FRPGItemRepositoryCommitRequest Request;
		if (!Value || !Value->TryGetString(Body) || Body.Len() > 64 * 1024 ||
			!FRPGItemBackendJsonCodec::DeserializeCommitRequest(Body, Request) || !ValidRequest(Request) ||
			!FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Canonical) || Canonical != Body || Out.Bodies.Contains(Request.RequestId)) return false;
		Out.Bodies.Add(Request.RequestId, MoveTemp(Body));
		Out.Requests.Add(Request.RequestId, MoveTemp(Request));
	}
	return true;
}

bool FRPGInventoryCommandJournal::LoadLocked()
{
	bHealthy = false;
	if (Directory.IsEmpty() || !Scope.IsValid() || !IFileManager::Get().MakeDirectory(*Directory, true)) return false;
	const FString Temporary = Path + TEXT(".tmp");
	if (IFileManager::Get().DirectoryExists(*Path) || IFileManager::Get().DirectoryExists(*Temporary)) return false;
	const bool bFinal = IFileManager::Get().FileExists(*Path), bTemporary = IFileManager::Get().FileExists(*Temporary);
	FState Final, Pending;
	if ((bFinal && !Read(Path, Final)) || (bTemporary && !Read(Temporary, Pending))) return false;
	if (bFinal && bTemporary)
	{
		const bool bSame = Final.Generation == Pending.Generation && Final.bRevoked == Pending.bRevoked && Final.Bodies.OrderIndependentCompareEqual(Pending.Bodies);
		if (!bSame)
		{
			if (Final.Generation == MAX_uint64 || Pending.Generation != Final.Generation + 1 || (Final.bRevoked && !Pending.bRevoked)) return false;
			for (const auto& Entry : Final.Bodies)
				if (const auto* Body = Pending.Bodies.Find(Entry.Key); Body && *Body != Entry.Value) return false;
		}
	}
	if (bTemporary && !IFileManager::Get().Move(*Path, *Temporary, bFinal, false, false, true)) return false;
	State = bTemporary ? MoveTemp(Pending) : MoveTemp(Final);
	bHealthy = true;
	return true;
}

bool FRPGInventoryCommandJournal::Restore()
{
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid()) { bHealthy = false; return false; }
	return LoadLocked();
}

bool FRPGInventoryCommandJournal::WriteLocked(FState Next)
{
	if (State.Generation == MAX_uint64 || Next.Bodies.Num() > MaximumCommands) { bHealthy = false; return false; }
	Next.Generation = State.Generation + 1;
	const auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("backendUrl"), Scope.BackendUrl); Root->SetStringField(TEXT("serverId"), Scope.ServerId);
	Root->SetStringField(TEXT("sessionId"), Scope.SessionId); Root->SetStringField(TEXT("characterId"), Scope.CharacterId);
	Root->SetStringField(TEXT("steamId"), Scope.SteamId); Root->SetStringField(TEXT("generation"), LexToString(Next.Generation));
	Root->SetBoolField(TEXT("revoked"), Next.bRevoked);
	TArray<FGuid> Ids; Next.Bodies.GetKeys(Ids);
	Ids.Sort([](const FGuid& A, const FGuid& B) { return A.ToString() < B.ToString(); });
	TArray<TSharedPtr<FJsonValue>> Commands;
	for (const auto& Id : Ids) Commands.Add(MakeShared<FJsonValueString>(Next.Bodies.FindChecked(Id)));
	Root->SetArrayField(TEXT("commands"), Commands);
	FString Contents;
	if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Contents))) { bHealthy = false; return false; }
	const FTCHARToUTF8 Bytes(*Contents);
	if (Bytes.Length() > MaximumBytes) { bHealthy = false; return false; }
	const FString Temporary = Path + TEXT(".tmp");
	TUniquePtr<IFileHandle> Handle(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Temporary));
	if (!Handle || !Handle->Write(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()) || !Handle->Flush(true)) { bHealthy = false; return false; }
	Handle.Reset();
	if (!IFileManager::Get().Move(*Path, *Temporary, true, false, false, true)) { bHealthy = false; return false; }
	State = MoveTemp(Next);
	return true;
}

bool FRPGInventoryCommandJournal::Persist(const FRPGItemRepositoryCommitRequest& Request)
{
	if (!bHealthy || !ValidRequest(Request)) return false;
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid() || !LoadLocked()) { bHealthy = false; return false; }
	if (State.bRevoked) return false;
	FString Body;
	if (!FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body) || Body.Len() > 64 * 1024) return false;
	if (const auto* Existing = State.Bodies.Find(Request.RequestId)) return *Existing == Body;
	FState Next = State;
	Next.Requests.Add(Request.RequestId, Request); Next.Bodies.Add(Request.RequestId, MoveTemp(Body));
	return WriteLocked(MoveTemp(Next));
}

bool FRPGInventoryCommandJournal::ForgetVerified(const FRPGItemRepositoryCommitRequest& Request)
{
	if (!bHealthy || !ValidRequest(Request)) return false;
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid() || !LoadLocked()) { bHealthy = false; return false; }
	FString Body;
	const auto* Existing = State.Bodies.Find(Request.RequestId);
	if (State.bRevoked || !Existing || !FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body) || *Existing != Body) return false;
	FState Next = State;
	Next.Requests.Remove(Request.RequestId); Next.Bodies.Remove(Request.RequestId);
	return WriteLocked(MoveTemp(Next));
}

bool FRPGInventoryCommandJournal::Quarantine()
{
	FSystemWideCriticalSection Lock(LockName, FTimespan::FromMilliseconds(100));
	if (!Lock.IsValid() || !LoadLocked()) { bHealthy = false; return false; }
	if (State.bRevoked) return true;
	FState Next = State; Next.bRevoked = true;
	return WriteLocked(MoveTemp(Next)); // Retain every frozen command; quarantine is not rollback/resolution.
}
