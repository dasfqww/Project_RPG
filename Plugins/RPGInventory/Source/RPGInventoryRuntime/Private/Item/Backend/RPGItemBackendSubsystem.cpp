#include "Item/Backend/RPGItemBackendSubsystem.h"

#include "Async/Async.h"
#include "HAL/PlatformMisc.h"
#include "Item/Backend/RPGItemBackendTransport.h"
#include "Item/Transaction/RPGPickupGrantJournal.h"
#include "Item/Transaction/RPGPickupGrantReceiptPolicy.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemBackendSubsystem)

namespace
{
	bool IsItemBackendServerRuntime()
	{
#if UE_BUILD_SHIPPING
		return IsRunningDedicatedServer();
#else
		return IsRunningDedicatedServer()
			|| FParse::Param(
				FCommandLine::Get(),
				TEXT("RPGItemE2EServer"));
#endif
	}
}

void URPGItemBackendSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!IsItemBackendServerRuntime())
	{
		return;
	}

	ServiceToken = FPlatformMisc::GetEnvironmentVariable(
		TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).TrimStartAndEnd();
	if (ServiceToken.IsEmpty())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Item Backend Gateway is unavailable because the dedicated "
				"server game-server token is not configured."));
		return;
	}

	const FString ServerId = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_GAME_SERVER_ID")).TrimStartAndEnd();
	FGuid Session;
	const FString SessionText = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).TrimStartAndEnd();
	if (ServerId.IsEmpty() || !FGuid::Parse(SessionText, Session) || !Session.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Item backend unavailable: assigned server/session scope is not configured."));
		return;
	}
	const FString SessionId = Session.ToString(EGuidFormats::DigitsWithHyphensLower);
	PickupJournal = MakeShared<FRPGPickupGrantJournal>(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ItemPickupJournal"), SessionId), ServerId, SessionId);
	if (!PickupJournal->Restore())
	{
		UE_LOG(LogTemp, Error, TEXT("Item backend disabled: pickup journal scope/contents cannot be restored. Files retained."));
		return;
	}
	for (const auto& Entry : PickupJournal->GetPending()) RestoredPickupIds.Add(Entry.Key);

	const TSharedRef<FRPGHttpItemBackendTransport, ESPMode::ThreadSafe>
		HttpTransport = MakeShared<
			FRPGHttpItemBackendTransport,
			ESPMode::ThreadSafe>(
			ApiUrl,
			ServiceToken,
			RequestTimeoutSeconds);
	Gateway = MakeShared<FRPGItemBackendGateway>(
		StaticCastSharedRef<IRPGItemBackendTransport>(HttpTransport),
		MaximumAttempts);
	UE_LOG(
		LogTemp,
		Log,
		TEXT("Item Backend Gateway initialized for the server runtime."));
}

void URPGItemBackendSubsystem::Deinitialize()
{
	Gateway.Reset();
	PickupJournal.Reset();
	RestoredPickupIds.Reset();
	InventoryJournals.Reset();
	ServiceToken.Reset();
	Super::Deinitialize();
}

bool URPGItemBackendSubsystem::IsAvailable() const
{
	return Gateway.IsValid() && PickupJournal.IsValid() && PickupJournal->IsHealthy();
}

TSharedPtr<FRPGInventoryCommandJournal> URPGItemBackendSubsystem::OpenInventoryJournal(
	const FString& SessionId, const FString& CharacterId, const FString& SteamId)
{
	if (!IsAvailable() || SessionId != PickupJournal->GetSessionId()) return {};
	FRPGInventoryJournalScope Scope{ApiUrl, PickupJournal->GetServerId(), SessionId, CharacterId, SteamId};
	if (!Scope.IsValid()) return {};
	const FString Root = InventoryJournalDirectory.IsEmpty()
		? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ItemInventoryJournal")) : InventoryJournalDirectory;
	auto Journal = MakeShared<FRPGInventoryCommandJournal>(Root, MoveTemp(Scope));
	if (const auto* Existing = InventoryJournals.Find(Journal->GetPath())) return *Existing;
	Journal->Restore(); // Failed/corrupt storage remains cached and blocked, never treated as an empty journal.
	InventoryJournals.Add(Journal->GetPath(), Journal);
	return Journal;
}

bool URPGItemBackendSubsystem::HasRestoredPickup(const FRPGItemOwnerRef& Owner) const
{
	if (!PickupJournal) return false;
	for (const FGuid& Id : RestoredPickupIds)
	{
		const auto* Request = PickupJournal->GetPending().Find(Id);
		if (Request && Request->Actor == Owner) return true;
	}
	return false;
}

void URPGItemBackendSubsystem::ReconcileRestoredPickups(
	const FRPGItemOwnerRef& Owner, TFunction<void(bool)> Completion)
{
	TArray<FRPGItemRepositoryCommitRequest> Requests;
	for (const FGuid& Id : RestoredPickupIds)
	{
		const auto* Request = PickupJournal->GetPending().Find(Id);
		if (Request && Request->Actor == Owner) Requests.Add(*Request);
	}
	if (Requests.IsEmpty()) { Completion(true); return; }
	struct FBatch
	{
		int32 Remaining = 0;
		TFunction<void(bool)> Completion;
	};
	const auto Batch = MakeShared<FBatch>();
	Batch->Remaining = Requests.Num();
	Batch->Completion = MoveTemp(Completion);
	const TWeakObjectPtr<URPGItemBackendSubsystem> WeakThis(this);
	const TSharedPtr<FRPGPickupGrantJournal> ExpectedJournal = PickupJournal;
	const TSharedPtr<FRPGItemBackendGateway> CurrentGateway = Gateway;
	for (const auto& Request : Requests)
	{
		FRPGItemBackendCommitCompletion Callback =
			[WeakThis, ExpectedJournal, Batch, Owner](FRPGItemBackendCommitResult Result) mutable
			{
				auto Apply = [WeakThis, ExpectedJournal, Batch, Owner, Result = MoveTemp(Result)]() mutable
				{
					URPGItemBackendSubsystem* Self = WeakThis.Get();
					const bool bCurrent = Self && Self->IsAvailable() && Self->PickupJournal == ExpectedJournal;
					// Only a complete, matching successful DB receipt resolves restart work.
					// Missing/auth/transport/rejection responses grant no permission to replay.
					if (bCurrent && Result.WasSuccessful() && ExpectedJournal->Resolve(Result))
						Self->RestoredPickupIds.Remove(Result.RequestId);
					if (--Batch->Remaining == 0)
					{
						auto Callback = MoveTemp(Batch->Completion);
						Callback(bCurrent && Self->IsAvailable() && !Self->HasRestoredPickup(Owner));
					}
				};
				if (IsInGameThread()) Apply();
				else AsyncTask(ENamedThreads::GameThread, MoveTemp(Apply));
			};
		const FGuid DropId = ExpectedJournal->GetDropId(Request.RequestId);
		if (DropId.IsValid())
		{
			FGuid Session; FGuid::Parse(ExpectedJournal->GetSessionId(), Session);
			CurrentGateway->LoadPickupReceipt(ExpectedJournal->GetServerId(), Session, DropId, Request,
				MoveTemp(Callback), [WeakThis, ExpectedJournal]()
				{
					const auto* Self = WeakThis.Get();
					return Self && Self->IsAvailable() && Self->PickupJournal == ExpectedJournal;
				});
		}
		else CurrentGateway->LoadCommitReceipt(Request, MoveTemp(Callback)); // v1 is never replayed/upgraded.
	}
}

bool URPGItemBackendSubsystem::LoadCharacterItems(
	const FString& CharacterId,
	FRPGItemBackendLoadCompletion Completion)
{
	if (!Completion) return false;
	FGuid CharacterGuid;
	if (!IsAvailable() ||
		!FGuid::Parse(CharacterId, CharacterGuid) || !CharacterGuid.IsValid())
	{
		if (Completion)
		{
			FRPGItemBackendLoadResult Result;
			Result.Status = IsAvailable()
				? ERPGItemBackendStatus::InvalidRequest
				: ERPGItemBackendStatus::Unavailable;
			Result.Error = IsAvailable()
				? TEXT("The backend character ID is invalid.")
				: TEXT("The item backend gateway is unavailable.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = CharacterGuid.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	const TWeakObjectPtr<URPGItemBackendSubsystem> WeakThis(this);
	ReconcileRestoredPickups(Owner,
		[WeakThis, Owner, Completion = MoveTemp(Completion)](bool bResolved) mutable
		{
			URPGItemBackendSubsystem* Self = WeakThis.Get();
			if (!Self || !Self->IsAvailable() || !bResolved)
			{
				FRPGItemBackendLoadResult Result;
				Result.Status = ERPGItemBackendStatus::Unavailable;
				Result.Error = TEXT("Unresolved pickup journal prevents this character's item load. No command was replayed.");
				if (Completion) Completion(MoveTemp(Result));
				return;
			}
			Self->Gateway->LoadItems(Owner, false, 500, MoveTemp(Completion));
		});
	return true;
}

bool URPGItemBackendSubsystem::Commit(
	const FRPGItemRepositoryCommitRequest& Request,
	FRPGItemBackendCommitCompletion Completion)
{
	if (!Completion) return false;
	const bool bPickup = Request.Operation == FName(TEXT("Item.Pickup"));
	if (bPickup || !IsAvailable() || HasRestoredPickup(Request.Actor))
	{
		if (Completion)
		{
			FRPGItemBackendCommitResult Result;
			Result.Status = bPickup ? ERPGItemBackendStatus::InvalidRequest : ERPGItemBackendStatus::Unavailable;
			Result.RequestId = Request.RequestId;
			Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint;
			Result.Actor = Request.Actor;
			Result.Error = TEXT("Item backend unavailable or pickup missing its prepared drop identity.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	Gateway->Commit(Request,
		[Completion = MoveTemp(Completion)](FRPGItemBackendCommitResult Result) mutable
		{
			auto Apply = [Completion = MoveTemp(Completion), Result = MoveTemp(Result)]() mutable
			{
				if (Completion) Completion(MoveTemp(Result));
			};
			if (IsInGameThread()) Apply();
			else AsyncTask(ENamedThreads::GameThread, MoveTemp(Apply));
		});
	return true;
}

bool URPGItemBackendSubsystem::CommitPickup(const FGuid& DropId, const FRPGItemRepositoryCommitRequest& Request,
	FRPGItemBackendCommitCompletion Completion, TFunction<bool()> CanContinue)
{
	if (!Completion) return false;
	if (!DropId.IsValid() || Request.Operation != FName(TEXT("Item.Pickup")) || !IsAvailable() ||
		HasRestoredPickup(Request.Actor) || (CanContinue && !CanContinue()) || !PickupJournal->Persist(Request, DropId))
	{
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::Unavailable; Result.RequestId = Request.RequestId;
		Result.Operation = Request.Operation; Result.CommandFingerprint = Request.CommandFingerprint; Result.Actor = Request.Actor;
		Result.Error = TEXT("Prepared pickup unavailable; no request sent and existing journal retained.");
		Completion(MoveTemp(Result)); return false;
	}
	const TWeakObjectPtr<URPGItemBackendSubsystem> WeakThis(this);
	const auto ExpectedJournal = PickupJournal;
	FGuid Session; FGuid::Parse(ExpectedJournal->GetSessionId(), Session);
	Gateway->CommitPickup(ExpectedJournal->GetServerId(), Session, DropId, Request,
		[WeakThis, ExpectedJournal, Completion = MoveTemp(Completion)](FRPGItemBackendCommitResult Result) mutable
		{
			auto* Self = WeakThis.Get();
			if (Self && Self->IsAvailable() && Self->PickupJournal == ExpectedJournal && Result.WasSuccessful())
				ExpectedJournal->Resolve(Result); // v2 rejects all non-success cleanup, including slot conflicts.
			if (Completion) Completion(MoveTemp(Result));
		},
		[WeakThis, ExpectedJournal, CanContinue = MoveTemp(CanContinue)]()
		{
			const auto* Self = WeakThis.Get();
			return Self && Self->IsAvailable() && Self->PickupJournal == ExpectedJournal && (!CanContinue || CanContinue());
		});
	return true;
}
