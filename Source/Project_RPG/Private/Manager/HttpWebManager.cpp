#include "Manager/HttpWebManager.h"
#include "Online/RPGServiceStatus.h"
#include "HAL/PlatformTime.h"
#include "HttpWebManagerRuntime.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Online/RPGJoinTicketAdmission.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Item/Backend/RPGItemBackendSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"

namespace
{
	using RPGHttpWebManager::IsHttpBackendServerRuntime;

	FAutoConsoleCommand DungeonRecoveryDiagnosticsCommand(TEXT("RPG.Dungeon.RecoveryStatus"),
		TEXT("Server-local read-only recovery JSON. Optional maximum intents (1..64); no retry/reset/force-clear."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
			{
				int32 Limit = 32;
				if (Args.Num() > 1 || (Args.Num() == 1 && !LexTryParseString(Limit, *Args[0])))
				{
					Output.Log(TEXT("{\"available\":false,\"reason\":\"invalid_limit\"}"));
					return;
				}
				UGameInstance* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
				if (!IsInGameThread() || !IsHttpBackendServerRuntime() || !Instance || !World->IsGameWorld()
					|| World->bIsTearingDown || World->GetNetMode() == NM_Client || Instance->GetWorld() != World)
				{
					Output.Log(TEXT("{\"available\":false,\"reason\":\"server_world_required\"}"));
					return;
				}
				const auto* Web = Instance->GetSubsystem<UHttpWebManager>();
				Output.Log(Web ? *Web->BuildConfiguredDungeonRecoveryDiagnostics(Limit)
					: TEXT("{\"available\":false,\"reason\":\"manager_unavailable\"}"));
			}));
}

UHttpWebManager::UHttpWebManager()
{
	DungeonRecovery = MakeShared<FRPGDungeonConnectionRecovery>(*this);
	DungeonRewardSettlement = MakeShared<FRPGDungeonRewardSettlement>(*this);
	PlayerAuthentication = MakeShared<FRPGBackendAuthentication>(*this, PlayerAuth);
}
UObject& UHttpWebManager::GetRecoveryOwner() const { return *const_cast<UHttpWebManager*>(this); }
UGameInstance* UHttpWebManager::GetRecoveryGameInstance() const { return GetGameInstance(); }
const FString& UHttpWebManager::GetRecoveryApiUrl() const { return ApiUrl; }
const FString& UHttpWebManager::GetRecoveryServerId() const { return GameServerId; }
const FString& UHttpWebManager::GetRecoverySessionId() const { return ConfiguredDungeonSessionId; }
bool UHttpWebManager::HasRecoveryCredential() const { return !BackendServiceToken.IsEmpty(); }
bool UHttpWebManager::HasValidRecoveryServiceCredential() const
{
	if (BackendServiceToken.IsEmpty() || BackendServiceToken.Len() > 4096) return false;
	for (const TCHAR Character : BackendServiceToken)
		if (Character <= TEXT(' ') || Character >= TEXT('\x7f')) return false;
	return true;
}
bool UHttpWebManager::IsRecoverySettlementActive() const { return DungeonRewardSettlement->IsActive(); }
void UHttpWebManager::ApplyRecoveryServiceAuthorization(const FHttpRequestRef& Request) const
{ Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BackendServiceToken)); }
float UHttpWebManager::GetRecoveryRequestTimeout() const { return RequestTimeoutSeconds; }
bool UHttpWebManager::IsRecoveryServerRuntime() const { return IsHttpBackendServerRuntime(); }
bool UHttpWebManager::IsRecoveryServerConfigured() const { return IsConfiguredForDungeonServer(); }
void UHttpWebManager::ApplyRecoveryHeaders(const FHttpRequestRef& Request) const { ApplyCommonHeaders(Request); }
void UHttpWebManager::BroadcastConsumedTicket(const FString& Ticket, const FString& Session,
	const FString& Character, const FString& Steam, int64 Generation, bool bSuccess)
{ OnJoinTicketConsumed.Broadcast(Ticket, Session, Character, Steam, Generation, bSuccess); }

void UHttpWebManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDungeonMembershipTransportShutdown = false;
	if (IsHttpBackendServerRuntime())
	{
		// The Item V2 gateway is server-owned.  Make its initialization order
		// explicit instead of relying on subsystem discovery during startup.
		Collection.InitializeDependency<URPGItemBackendSubsystem>();

		BackendServiceToken = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).TrimStartAndEnd();
		GameServerId = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_GAME_SERVER_ID")).TrimStartAndEnd();
		ConfiguredDungeonSessionId = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).TrimStartAndEnd();
		if (BackendServiceToken.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Dedicated Server backend game-server token is not configured. "
					"Persistence requests may be rejected."));
		}
		if (GameServerId.IsEmpty() || ConfiguredDungeonSessionId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Dedicated Server dungeon assignment is incomplete. Set "
					"PROJECT_RPG_GAME_SERVER_ID and "
					"PROJECT_RPG_DUNGEON_SESSION_ID."));
		}
	}
	DungeonRecovery->Initialize();
	UE_LOG(LogTemp, Log, TEXT("HttpWebManager initialized."));
}

void UHttpWebManager::Deinitialize()
{
	if (ActivePlayerProgressionRequest.IsValid())
	{
		ActivePlayerProgressionRequest->OnProcessRequestComplete().Unbind();
		ActivePlayerProgressionRequest->CancelRequest();
		ActivePlayerProgressionRequest.Reset();
	}
	PlayerAuthentication->CancelLogout();
	bDungeonMembershipTransportShutdown = true;
	DungeonRecovery->Shutdown();
	CancelDungeonConnectionMutations();
	CancelDungeonMembershipRequest();
	InvalidatePlayerDungeonRequests();
	PlayerAuthentication->CancelLogin();
	OnInventoryLoaded.Clear();
	OnCharacterInventoryLoaded.Clear();
	OnInventorySaved.Clear();
	OnCharacterInventorySaved.Clear();
	OnBackendAuthenticationCompleted.Clear();
	OnBackendServiceDraining.Clear();
	OnCharactersLoaded.Clear();
	OnCharacterCreated.Clear();
	++CharacterCreationOptionsGeneration;
	OnCharacterCreationOptionsLoaded.Clear();
	OnDungeonSessionUpdated.Clear();
	OnDungeonAbandonmentCompleted.Clear();
	OnJoinTicketIssued.Clear();
	OnJoinTicketResult.Clear();
	OnJoinTicketConsumed.Clear();
	OnDungeonRewardSettlementRequested.Clear();
	PlayerAuth.Reset();
	BackendServiceToken.Reset();
	GameServerId.Reset();
	ConfiguredDungeonSessionId.Reset();
	Super::Deinitialize();
}

void UHttpWebManager::InvalidatePlayerDungeonRequests()
{
	// Server heartbeat/result callbacks must never be invalidated by a client
	// escape path. In-flight HTTP writes are not rolled back by local travel.
	if (!IsHttpBackendServerRuntime()) ++PlayerDungeonRequestGeneration;
}

void UHttpWebManager::ApplyCommonHeaders(const FHttpRequestRef& Request) const
{
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	const FString Authorization = IsPlayerLogoutInFlight() ? FString() : PlayerAuth.Authorization(FPlatformTime::Seconds());
	if (!Authorization.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), Authorization);
	}
	else if (!BackendServiceToken.IsEmpty())
	{
		Request->SetHeader(
			TEXT("Authorization"),
			FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	}
}

void UHttpWebManager::RequestJoinTicket(
	const FString& CharacterId,
	const FString& DungeonSessionId)
{
	if (CharacterId.IsEmpty()
		|| DungeonSessionId.IsEmpty()
		|| !HasAccessToken())
	{
		BroadcastJoinTicketResult(
			DungeonSessionId,
			CharacterId,
			FString(),
			FString(),
			false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	RootObject->SetStringField(
		TEXT("dungeonSessionId"), DungeonSessionId);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		BroadcastJoinTicketResult(
			DungeonSessionId,
			CharacterId,
			FString(),
			FString(),
			false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnRequestJoinTicketResponseReceived,
		CharacterId,
		DungeonSessionId,
		PlayerDungeonRequestGeneration);
	Request->SetURL(ApiUrl / TEXT("join-tickets"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::ConsumeJoinTicket(const FString& Ticket, const FString& Steam)
{ DungeonRecovery->ConsumeJoinTicket(Ticket, Steam); }
void UHttpWebManager::ObserveDungeonConsumeConnectionLoss(const FString& Ticket, const FString& Steam, FDateTime ObservedAt)
{ DungeonRecovery->ObserveDungeonConsumeConnectionLoss(Ticket, Steam, ObservedAt); }
bool UHttpWebManager::HasExpiredDungeonConsumeRecovery(const FString& Ticket) const
{ return DungeonRecovery->HasExpiredDungeonConsumeRecovery(Ticket); }
bool UHttpWebManager::CanDispatchConfiguredDungeonConnectionMutation() const
{ return DungeonRecovery->CanDispatchConfiguredDungeonConnectionMutation(); }
FString UHttpWebManager::BuildConfiguredDungeonRecoveryDiagnostics(int32 MaxIntents) const
{ return DungeonRecovery->BuildConfiguredDungeonRecoveryDiagnostics(MaxIntents); }
bool UHttpWebManager::ConfirmConfiguredDungeonMemberAdmission(const FString& Session, const FString& Character,
	const FString& Steam, int64 Generation, TFunction<void(bool, int32)> Completion)
{ return DungeonRecovery->ConfirmConfiguredDungeonMemberAdmission(Session, Character, Steam, Generation, MoveTemp(Completion)); }
bool UHttpWebManager::ReportConfiguredDungeonMemberDisconnection(const FString& Session, const FString& Character,
	const FString& Steam, int64 Generation, FDateTime ObservedAt)
{ return DungeonRecovery->ReportConfiguredDungeonMemberDisconnection(Session, Character, Steam, Generation, ObservedAt); }
UHttpWebManager::FDungeonMutationScope UHttpWebManager::CaptureDungeonMutationScope() const
{ return DungeonRecovery->CaptureDungeonMutationScope(); }
bool UHttpWebManager::IsCurrentDungeonMutationScope(const FDungeonMutationScope& Scope) const
{ return DungeonRecovery->IsCurrentDungeonMutationScope(Scope); }
void UHttpWebManager::CancelDungeonConnectionMutations()
{
	DungeonRecovery->CancelDungeonConnectionMutations();
	DungeonRewardSettlement->Cancel();
}

void UHttpWebManager::CancelDungeonMembershipRequest()
{
	++DungeonMembershipRequestGeneration;
	if (!ActiveDungeonMembershipRequest.IsValid()) return;
	ActiveDungeonMembershipRequest->OnProcessRequestComplete().Unbind();
	ActiveDungeonMembershipRequest->CancelRequest();
	ActiveDungeonMembershipRequest.Reset();
}

FHttpRequestPtr UHttpWebManager::CreateConfiguredDungeonMembershipRequest(
	TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion)
{
	FRPGDungeonMembershipMonitor Scope;
	if (bDungeonMembershipTransportShutdown || ActiveDungeonMembershipRequest.IsValid()
		|| !IsConfiguredForDungeonServer() || !Scope.Configure(ConfiguredDungeonSessionId, GameServerId)) return nullptr;
	if (BackendServiceToken.Len() > 4096) return nullptr;
	for (const TCHAR Character : BackendServiceToken)
		if (Character <= TEXT(' ') || Character >= TEXT('\x7f')) return nullptr;
	const FString ExpectedSessionId = ConfiguredDungeonSessionId;
	const FString ExpectedServerId = GameServerId;
	const uint64 Generation = ++DungeonMembershipRequestGeneration;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	// This completion now revokes live gameplay authority and closes connections.
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	ActiveDungeonMembershipRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Generation, ExpectedSessionId, ExpectedServerId, Completion = MoveTemp(Completion)]
		(FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response, bool bSuccess) mutable
		{
			if (bDungeonMembershipTransportShutdown || Generation != DungeonMembershipRequestGeneration
				|| CompletedRequest != ActiveDungeonMembershipRequest) return;
			ActiveDungeonMembershipRequest.Reset();
			FRPGDungeonMembershipSnapshotResult Result;
			Result.HttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
			if (ConfiguredDungeonSessionId == ExpectedSessionId && GameServerId == ExpectedServerId
				&& bSuccess && Response.IsValid() && Response->GetContentLength() <= 64 * 1024
				&& Response->GetContent().Num() <= 64 * 1024)
				Result.bSuccess = FRPGDungeonMembershipSnapshotCodec::Decode(Response->GetContentAsString(),
					Result.HttpStatus, ExpectedSessionId, ExpectedServerId, Result.Snapshot);
			if (Completion) Completion(MoveTemp(Result));
		});
	Request->SetURL(ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/membership-snapshot"),
		*FGenericPlatformHttp::UrlEncode(ExpectedSessionId)));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
	// ApplyCommonHeaders prefers a player token. This authority boundary must
	// never use it or fall back to a player/admin credential.
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds)
		? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request;
}

bool UHttpWebManager::LoadConfiguredDungeonMembershipSnapshot(
	TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion)
{
	if (!IsHttpBackendServerRuntime()) return false;
	FHttpRequestPtr Request = CreateConfiguredDungeonMembershipRequest(MoveTemp(Completion));
	if (!Request.IsValid()) return false;
	if (Request->ProcessRequest()) return true;
	if (ActiveDungeonMembershipRequest == Request) CancelDungeonMembershipRequest();
	return false;
}

FHttpRequestPtr UHttpWebManager::CreateConfiguredDungeonDisconnectionExpirationsRequest(
	TFunction<void()> Completion)
{
	FRPGDungeonMembershipMonitor Scope;
	if (bDungeonMembershipTransportShutdown || ActiveDungeonMembershipRequest.IsValid()
		|| !IsConfiguredForDungeonServer() || !Scope.Configure(ConfiguredDungeonSessionId, GameServerId)) return nullptr;
	if (BackendServiceToken.Len() > 4096) return nullptr;
	for (const TCHAR Character : BackendServiceToken)
		if (Character <= TEXT(' ') || Character >= TEXT('\x7f')) return nullptr;
	const FString ExpectedSessionId = ConfiguredDungeonSessionId;
	const FString ExpectedServerId = GameServerId;
	const uint64 Generation = ++DungeonMembershipRequestGeneration;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	ActiveDungeonMembershipRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Generation, ExpectedSessionId, ExpectedServerId, Completion = MoveTemp(Completion)]
		(FHttpRequestPtr CompletedRequest, FHttpResponsePtr, bool) mutable
		{
			if (bDungeonMembershipTransportShutdown || Generation != DungeonMembershipRequestGeneration
				|| CompletedRequest != ActiveDungeonMembershipRequest) return;
			ActiveDungeonMembershipRequest.Reset();
			// A POST response alone is never ejection evidence. Even 403 after the
			// final member leaves must be followed by the grace-window GET.
			if (ConfiguredDungeonSessionId == ExpectedSessionId && GameServerId == ExpectedServerId
				&& Completion) Completion();
		});
	Request->SetURL(ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/disconnection-expirations"),
		*FGenericPlatformHttp::UrlEncode(ExpectedSessionId)));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds)
		? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request;
}

bool UHttpWebManager::ProcessConfiguredDungeonDisconnectionExpirations(TFunction<void()> Completion)
{
	if (!IsHttpBackendServerRuntime()) return false;
	FHttpRequestPtr Request = CreateConfiguredDungeonDisconnectionExpirationsRequest(MoveTemp(Completion));
	if (!Request.IsValid()) return false;
	if (Request->ProcessRequest()) return true;
	if (ActiveDungeonMembershipRequest == Request) CancelDungeonMembershipRequest();
	return false;
}

bool UHttpWebManager::ConnectWithJoinTicket(
	APlayerController* PlayerController,
	const FString& ServerAddress,
	const FString& JoinTicket)
{
	if (!IsValid(PlayerController)
		|| ServerAddress.IsEmpty()
		|| JoinTicket.IsEmpty())
	{
		return false;
	}

	const FString EncodedTicket =
		FGenericPlatformHttp::UrlEncode(JoinTicket);
	const FString TravelUrl = FString::Printf(
		TEXT("%s?JoinTicket=%s"),
		*ServerAddress,
		*EncodedTicket);
	PlayerController->ClientTravel(TravelUrl, TRAVEL_Absolute);
	return true;
}

void UHttpWebManager::BroadcastJoinTicketResult(const FString& DungeonSessionId, const FString& CharacterId,
	const FString& JoinTicket, const FString& ExpiresAt, const bool bSuccess, const FString& ErrorCode)
{
	OnJoinTicketResult.Broadcast(DungeonSessionId, CharacterId, JoinTicket, ExpiresAt, bSuccess, ErrorCode);
	OnJoinTicketIssued.Broadcast(DungeonSessionId, CharacterId, JoinTicket, ExpiresAt, bSuccess);
}

void UHttpWebManager::OnRequestJoinTicketResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString CharacterId,
	FString DungeonSessionId,
	uint64 RequestGeneration)
{
	if (RequestGeneration != PlayerDungeonRequestGeneration) return;
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	FString JoinTicket;
	FString ExpiresAt;
	FString ErrorCode;

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString ResponseCharacterId;
		FString ResponseDungeonSessionId;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(
				TEXT("dungeonSessionId"), ResponseDungeonSessionId)
			&& RootObject->TryGetStringField(
				TEXT("characterId"), ResponseCharacterId)
			&& RootObject->TryGetStringField(TEXT("joinTicket"), JoinTicket)
			&& RootObject->TryGetStringField(TEXT("expiresAt"), ExpiresAt)
			&& ResponseDungeonSessionId == DungeonSessionId
			&& ResponseCharacterId == CharacterId
			&& !JoinTicket.IsEmpty();
	}

	if (!bSuccess)
	{
		JoinTicket.Reset();
		ExpiresAt.Reset();
		// Only the defined conflict codes are UI state. Never forward arbitrary
		// HTTP bodies, transport errors or a forged code from a successful response.
		if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 409
			&& Response->GetContentLength() <= 4096)
		{
			TSharedPtr<FJsonObject> ErrorObject;
			FString Code;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), ErrorObject)
				&& ErrorObject.IsValid() && ErrorObject->TryGetStringField(TEXT("error"), Code)
				&& (Code.Equals(TEXT("dungeon_reconnect_grace_expired"), ESearchCase::CaseSensitive)
					|| Code.Equals(TEXT("dungeon_member_not_active"), ESearchCase::CaseSensitive)))
				ErrorCode = Code;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("Join ticket request failed for character %s (HTTP %d)."),
			*CharacterId,
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	BroadcastJoinTicketResult(
		DungeonSessionId,
		CharacterId,
		JoinTicket,
		ExpiresAt,
		bSuccess, ErrorCode);
}

void UHttpWebManager::NotifyServiceDraining(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response)
{
	FString Prefix = ApiUrl; Prefix.RemoveFromEnd(TEXT("/")); Prefix += TEXT("/");
	if (Request && Response && Request->GetURL().StartsWith(Prefix)
		&& FRPGServiceStatusSnapshot::IsDrainResponse(Response->GetResponseCode(), Response->GetContentAsString()))
	{
		UE_LOG(LogTemp, Log, TEXT("Backend service_draining refusal received for current backend scope."));
		OnBackendServiceDraining.Broadcast(ApiUrl);
	}
}
