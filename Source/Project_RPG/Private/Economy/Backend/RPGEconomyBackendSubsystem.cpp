#include "Economy/Backend/RPGEconomyBackendSubsystem.h"

#include "Economy/Backend/RPGEconomyBackendTransport.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGEconomyBackendSubsystem)

void URPGEconomyBackendSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!IsRunningDedicatedServer())
	{
		return;
	}

	GameServerToken = FPlatformMisc::GetEnvironmentVariable(
		TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).TrimStartAndEnd();
	DungeonSessionId = FPlatformMisc::GetEnvironmentVariable(
		TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).TrimStartAndEnd();
	FGuid ParsedDungeonSessionId;
	if (GameServerToken.IsEmpty()
		|| !FGuid::Parse(DungeonSessionId, ParsedDungeonSessionId))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("Economy Backend Gateway is unavailable because the dedicated "
				"server token or dungeon session ID is not configured."));
		return;
	}
	DungeonSessionId = ParsedDungeonSessionId.ToString(
		EGuidFormats::DigitsWithHyphensLower);

	const TSharedRef<FRPGHttpEconomyBackendTransport, ESPMode::ThreadSafe>
		HttpTransport = MakeShared<
			FRPGHttpEconomyBackendTransport,
			ESPMode::ThreadSafe>(
			ApiUrl,
			GameServerToken,
			RequestTimeoutSeconds);
	Gateway = MakeShared<FRPGEconomyBackendGateway>(
		StaticCastSharedRef<IRPGEconomyBackendTransport>(HttpTransport),
		MaximumAttempts);
}

void URPGEconomyBackendSubsystem::Deinitialize()
{
	TransactionCommittedEvent.Clear();
	Gateway.Reset();
	GameServerToken.Reset();
	DungeonSessionId.Reset();
	Super::Deinitialize();
}

bool URPGEconomyBackendSubsystem::LoadWallet(
	const FString& CharacterId,
	FRPGEconomyWalletCompletion Completion) const
{
	if (!Gateway.IsValid())
	{
		if (Completion)
		{
			FRPGEconomyWalletResult Result;
			Result.Status = ERPGEconomyBackendStatus::Unavailable;
			Result.Error = TEXT("The economy backend gateway is unavailable.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	Gateway->LoadWallet(
		CharacterId,
		DungeonSessionId,
		MoveTemp(Completion));
	return true;
}

bool URPGEconomyBackendSubsystem::Commit(
	const FRPGEconomyTransactionRequest& Request,
	FRPGEconomyCommitCompletion Completion)
{
	if (!Gateway.IsValid())
	{
		if (Completion)
		{
			FRPGEconomyCommitResult Result;
			Result.Status = ERPGEconomyBackendStatus::Unavailable;
			Result.RequestId = Request.RequestId;
			Result.CharacterId = Request.CharacterId;
			Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint;
			Result.Reason = Request.Reason;
			Result.Error = TEXT("The economy backend gateway is unavailable.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	if (!Request.DungeonSessionId.IsEmpty()
		&& !Request.DungeonSessionId.Equals(
			DungeonSessionId,
			ESearchCase::IgnoreCase))
	{
		if (Completion)
		{
			FRPGEconomyCommitResult Result;
			Result.Status = ERPGEconomyBackendStatus::Forbidden;
			Result.RequestId = Request.RequestId;
			Result.CharacterId = Request.CharacterId;
			Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint;
			Result.Reason = Request.Reason;
			Result.Error = TEXT(
				"The transaction belongs to another dungeon session.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	FRPGEconomyTransactionRequest AuthorizedRequest = Request;
	AuthorizedRequest.DungeonSessionId = DungeonSessionId;
	TWeakObjectPtr<URPGEconomyBackendSubsystem> WeakThis(this);
	Gateway->Commit(
		AuthorizedRequest,
		[WeakThis, Completion = MoveTemp(Completion)](
			FRPGEconomyCommitResult Result) mutable
		{
			if (WeakThis.IsValid() && Result.WasSuccessful())
			{
				WeakThis->TransactionCommittedEvent.Broadcast(Result);
			}
			if (Completion)
			{
				Completion(MoveTemp(Result));
			}
		});
	return true;
}
