#include "Item/Backend/RPGItemBackendSubsystem.h"

#include "HAL/PlatformMisc.h"
#include "Item/Backend/RPGItemBackendTransport.h"
#include "Misc/App.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGItemBackendSubsystem)

void URPGItemBackendSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!IsRunningDedicatedServer())
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
}

void URPGItemBackendSubsystem::Deinitialize()
{
	Gateway.Reset();
	ServiceToken.Reset();
	Super::Deinitialize();
}

bool URPGItemBackendSubsystem::LoadCharacterItems(
	const FString& CharacterId,
	FRPGItemBackendLoadCompletion Completion) const
{
	FGuid CharacterGuid;
	if (!Gateway.IsValid() ||
		!FGuid::Parse(CharacterId, CharacterGuid))
	{
		if (Completion)
		{
			FRPGItemBackendLoadResult Result;
			Result.Status = Gateway.IsValid()
				? ERPGItemBackendStatus::InvalidRequest
				: ERPGItemBackendStatus::Unavailable;
			Result.Error = Gateway.IsValid()
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
	Gateway->LoadItems(
		Owner,
		false,
		500,
		MoveTemp(Completion));
	return true;
}

bool URPGItemBackendSubsystem::Commit(
	const FRPGItemRepositoryCommitRequest& Request,
	FRPGItemBackendCommitCompletion Completion) const
{
	if (!Gateway.IsValid())
	{
		if (Completion)
		{
			FRPGItemBackendCommitResult Result;
			Result.Status = ERPGItemBackendStatus::Unavailable;
			Result.RequestId = Request.RequestId;
			Result.Operation = Request.Operation;
			Result.CommandFingerprint = Request.CommandFingerprint;
			Result.Actor = Request.Actor;
			Result.Error = TEXT("The item backend gateway is unavailable.");
			Completion(MoveTemp(Result));
		}
		return false;
	}

	Gateway->Commit(Request, MoveTemp(Completion));
	return true;
}
