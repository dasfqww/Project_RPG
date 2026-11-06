#include "Manager/HttpWebManager.h"
#include "Online/RPGServiceStatus.h"
#include "HAL/PlatformTime.h"
#include "Economy/RPGDungeonRewardSnapshot.h"
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
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"

namespace
{

	bool ClaimDungeonSettlementCompletion(FHttpRequestPtr& Active, const FHttpRequestPtr& Completed)
	{
		if (!Active.IsValid() || Active != Completed) return false;
		Active.Reset();
		return true;
	}


	bool IsHttpBackendServerRuntime()
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

	const TCHAR* ToBackendBindState(const ERPGItemBindState BindState)
	{
		switch (BindState)
		{
		case ERPGItemBindState::BindOnEquip:
			return TEXT("BindOnEquip");
		case ERPGItemBindState::CharacterBound:
			return TEXT("CharacterBound");
		case ERPGItemBindState::AccountBound:
			return TEXT("AccountBound");
		default:
			return TEXT("Unbound");
		}
	}

	bool TryParseDungeonSession(
		const TSharedPtr<FJsonObject>& Object,
		FRPGDungeonSession& OutSession)
	{
		if (!Object.IsValid()
			|| !Object->TryGetStringField(
				TEXT("dungeonSessionId"), OutSession.DungeonSessionId)
			|| !Object->TryGetStringField(
				TEXT("dungeonId"), OutSession.DungeonId)
			|| !Object->TryGetStringField(
				TEXT("difficulty"), OutSession.Difficulty)
			|| !Object->TryGetStringField(TEXT("state"), OutSession.State)
			|| !Object->TryGetStringField(
				TEXT("expiresAt"), OutSession.ExpiresAt)
			|| OutSession.DungeonSessionId.IsEmpty())
		{
			return false;
		}

		Object->TryGetStringField(TEXT("serverId"), OutSession.ServerId);
		Object->TryGetStringField(
			TEXT("serverAddress"),
			OutSession.ServerAddress);
		// Missing metadata is a read-only legacy session, never implicit leader authority.
		Object->TryGetStringField(TEXT("leaderCharacterId"), OutSession.LeaderCharacterId);
		Object->TryGetStringField(TEXT("departureRequestId"), OutSession.DepartureRequestId);
		Object->TryGetStringField(TEXT("departureRequestedAt"), OutSession.DepartureRequestedAt);
		double Revision = 0, RequestRevision = 0;
		if (Object->HasField(TEXT("lobbyRevision")) && (!Object->TryGetNumberField(TEXT("lobbyRevision"), Revision)
			|| !FMath::IsFinite(Revision) || Revision < 0 || Revision > 9007199254740991.0 || FMath::FloorToDouble(Revision) != Revision)) return false;
		if (Object->TryGetNumberField(TEXT("departureRequestRevision"), RequestRevision)
			&& (!FMath::IsFinite(RequestRevision) || RequestRevision < 1 || RequestRevision > 9007199254740991.0
				|| FMath::FloorToDouble(RequestRevision) != RequestRevision)) return false;
		OutSession.LobbyRevision = static_cast<int64>(Revision);
		OutSession.DepartureRequestRevision = static_cast<int64>(RequestRevision);
		FGuid Leader, Request;
		if ((!OutSession.LeaderCharacterId.IsEmpty() && !FGuid::Parse(OutSession.LeaderCharacterId, Leader))
			|| (!OutSession.DepartureRequestId.IsEmpty() && (!FGuid::Parse(OutSession.DepartureRequestId, Request) || RequestRevision < 1))
			|| (!OutSession.DepartureRequestedAt.IsEmpty() && OutSession.DepartureRequestId.IsEmpty())) return false;

		const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
		if (!Object->TryGetArrayField(TEXT("members"), MemberValues)
			|| !MemberValues)
		{
			return false;
		}

		OutSession.Members.Reset(MemberValues->Num());
		for (const TSharedPtr<FJsonValue>& MemberValue : *MemberValues)
		{
			const TSharedPtr<FJsonObject> MemberObject =
				MemberValue.IsValid() ? MemberValue->AsObject() : nullptr;
			FRPGDungeonSessionMember Member;
			if (!MemberObject.IsValid()
				|| !MemberObject->TryGetStringField(
					TEXT("characterId"), Member.CharacterId)
				|| !MemberObject->TryGetStringField(
					TEXT("joinedAt"), Member.JoinedAt)
				|| !MemberObject->TryGetStringField(
					TEXT("leaseExpiresAt"), Member.LeaseExpiresAt)
				|| Member.CharacterId.IsEmpty())
			{
				OutSession = FRPGDungeonSession();
				return false;
			}

			// Optional display metadata. Missing/null/invalid-type names must not reject valid legacy membership.
			if (MemberObject->HasTypedField<EJson::String>(TEXT("characterName")))
				MemberObject->TryGetStringField(TEXT("characterName"), Member.CharacterName);
			OutSession.Members.Add(MoveTemp(Member));
		}

		return true;
	}
}

UHttpWebManager::UHttpWebManager()
{
	DungeonRecovery = MakeShared<FRPGDungeonConnectionRecovery>(*this);
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
bool UHttpWebManager::IsRecoverySettlementActive() const { return ActiveDungeonSettlementRequest.IsValid(); }
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
	PlayerLogoutContextValidator = nullptr;
	PlayerLogoutCompletion = nullptr;
	if (ActivePlayerLogoutRequest.IsValid())
	{
		ActivePlayerLogoutRequest->OnProcessRequestComplete().Unbind();
		ActivePlayerLogoutRequest->CancelRequest();
		ActivePlayerLogoutRequest.Reset();
	}
	bDungeonMembershipTransportShutdown = true;
	DungeonRecovery->Shutdown();
	CancelDungeonConnectionMutations();
	CancelDungeonMembershipRequest();
	InvalidatePlayerDungeonRequests();
	if (ActiveAuthenticationRequest.IsValid())
	{
		ActiveAuthenticationRequest->OnProcessRequestComplete().Unbind();
		ActiveAuthenticationRequest->CancelRequest();
		ActiveAuthenticationRequest.Reset();
	}
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
	ActiveAuthenticationRequestStartedSeconds = -1.0;
	BackendServiceToken.Reset();
	GameServerId.Reset();
	ConfiguredDungeonSessionId.Reset();
	Super::Deinitialize();
}

void UHttpWebManager::SetAccessToken(const FString& InAccessToken)
{
	if (IsPlayerLogoutInFlight()) return;
	PlayerAuth.SetOpaqueToken(InAccessToken);
}

void UHttpWebManager::ClearAccessToken()
{
	PlayerAuth.Invalidate();
}

void UHttpWebManager::InvalidatePlayerDungeonRequests()
{
	// Server heartbeat/result callbacks must never be invalidated by a client
	// escape path. In-flight HTTP writes are not rolled back by local travel.
	if (!IsHttpBackendServerRuntime()) ++PlayerDungeonRequestGeneration;
}

bool UHttpWebManager::HasAccessToken() const
{
	return !IsPlayerLogoutInFlight() && PlayerAuth.HasAccessToken(FPlatformTime::Seconds());
}

bool UHttpWebManager::RequestPlayerLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion)
{
	if (!IsInGameThread() || IsHttpBackendServerRuntime() || IsPlayerLogoutInFlight() || ActiveAuthenticationRequest.IsValid()
		|| IsPlayerProgressionRequestInFlight() || !HasPlayerLogoutCredential() || !CanFinalize || !Completion) return false;
	const auto Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetURL(ApiUrl / TEXT("auth/logout"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), PlayerAuth.LogoutAuthorization());
	Request->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f));
	Request->OnProcessRequestComplete().BindUObject(this, &ThisClass::OnPlayerLogoutResponse);
	PlayerLogoutContextValidator = MoveTemp(CanFinalize);
	PlayerLogoutCompletion = MoveTemp(Completion);
	ActivePlayerLogoutRequest = Request;
	if (!Request->ProcessRequest()) OnPlayerLogoutResponse(Request, nullptr, false);
	return true;
}

void UHttpWebManager::OnPlayerLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	const bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActivePlayerLogoutRequest) return;
	const bool bSameCredential = PlayerAuth.MatchesRequest(Request->GetHeader(TEXT("Authorization")));
	const bool bConfirmed = bWasSuccessful && Response.IsValid()
		&& (Response->GetResponseCode() == 204 || Response->GetResponseCode() == 401);
	auto ValidateContext = MoveTemp(PlayerLogoutContextValidator);
	auto Completion = MoveTemp(PlayerLogoutCompletion);
	const bool bSuccess = bSameCredential && bConfirmed && ValidateContext && ValidateContext();
	if (bSuccess)
	{
		PlayerAuth.Reset();
		InvalidatePlayerDungeonRequests();
	}
	else if (bSameCredential && bConfirmed)
	{
		// The server may have revoked the token, but a changed world/selection
		// cannot authorize an account switch. Preserve the pinned identity.
		PlayerAuth.Invalidate();
	}
	ActivePlayerLogoutRequest.Reset();
	if (Completion) Completion(bSuccess);
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

void UHttpWebManager::AuthenticateWithSteamTicket(const FString& SteamTicketHex)
{
	// Duplicate calls must not complete/cancel the existing login.
	if (ActiveAuthenticationRequest.IsValid() || IsPlayerLogoutInFlight()) return;
	if (SteamTicketHex.IsEmpty())
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("ticket"), SteamTicketHex);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this, &ThisClass::OnAuthenticateResponseReceived);
	Request->SetURL(ApiUrl / TEXT("auth/steam-ticket"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	ActiveAuthenticationRequest = Request;
	ActiveAuthenticationRequestStartedSeconds = FPlatformTime::Seconds();
	if (!Request->ProcessRequest() && ActiveAuthenticationRequest == Request)
	{
		ActiveAuthenticationRequest.Reset();
		ActiveAuthenticationRequestStartedSeconds = -1.0;
		OnBackendAuthenticationCompleted.Broadcast(false);
	}
}

void UHttpWebManager::LoadCharacters()
{
	if (!HasAccessToken())
	{
		const TArray<FRPGBackendCharacter> EmptyCharacters;
		OnCharactersLoaded.Broadcast(EmptyCharacters, false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnLoadCharactersResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::CreateCharacter(const FString& CharacterName, const FString& ClassId)
{
	const FString NormalizedName = CharacterName.TrimStartAndEnd();
	if (!HasAccessToken() || NormalizedName.IsEmpty())
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("name"), NormalizedName);
	if (!ClassId.IsEmpty()) RootObject->SetStringField(TEXT("classId"), ClassId);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnCreateCharacterResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::CreateDungeonSession(
	const FString& CharacterId,
	const FString& DungeonId,
	const FString& Difficulty)
{
	const FString NormalizedDungeonId = DungeonId.TrimStartAndEnd();
	const FString NormalizedDifficulty = Difficulty.TrimStartAndEnd();
	if (!HasAccessToken()
		|| CharacterId.IsEmpty()
		|| NormalizedDungeonId.IsEmpty()
		|| NormalizedDifficulty.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	RootObject->SetStringField(TEXT("dungeonId"), NormalizedDungeonId);
	RootObject->SetStringField(TEXT("difficulty"), NormalizedDifficulty);
	SendDungeonSessionRequest(
		TEXT("dungeon-sessions"),
		TEXT("POST"),
		RootObject,
		TEXT("create"),
		FString());
}

void UHttpWebManager::JoinDungeonSession(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (!HasAccessToken()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("POST"),
		RootObject,
		TEXT("join"),
		DungeonSessionId);
}

void UHttpWebManager::LoadDungeonSession(const FString& DungeonSessionId)
{
	if (!HasAccessToken() || DungeonSessionId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("GET"),
		nullptr,
		TEXT("load"),
		DungeonSessionId);
}

void UHttpWebManager::LoadActiveDungeonSession(
	const FString& CharacterId)
{
	const FString NormalizedCharacterId = CharacterId.TrimStartAndEnd();
	if (!HasAccessToken() || NormalizedCharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("characters/%s/active-dungeon-session"),
			*FGenericPlatformHttp::UrlEncode(NormalizedCharacterId)),
		TEXT("GET"),
		nullptr,
		TEXT("resume"),
		FString());
}

void UHttpWebManager::LeaveDungeonSession(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (!HasAccessToken()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId),
			*FGenericPlatformHttp::UrlEncode(CharacterId)),
		TEXT("DELETE"),
		nullptr,
		TEXT("leave"),
		DungeonSessionId);
}

void UHttpWebManager::SetDungeonDeparture(const FString& SessionId, const FString& CharacterId,
	const FString& RequestId, int64 ExpectedRevision, bool bCancel)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("characterId"), CharacterId);
	Body->SetStringField(TEXT("requestId"), RequestId);
	Body->SetNumberField(TEXT("expectedLobbyRevision"), static_cast<double>(ExpectedRevision));
	SendDungeonSessionRequest(FString::Printf(TEXT("dungeon-sessions/%s/departure%s"),
		*FGenericPlatformHttp::UrlEncode(SessionId), bCancel ? TEXT("/cancel") : TEXT("")),
		TEXT("POST"), Body, TEXT("departure"), SessionId);
}

void UHttpWebManager::AbandonDungeonSession(const FString& DungeonSessionId,
	const FString& CharacterId, const FString& ExpectedSteamId)
{
	FRPGDungeonAbandonmentReceipt Identity;
	Identity.DungeonSessionId = DungeonSessionId;
	Identity.CharacterId = CharacterId;
	FGuid SessionGuid, CharacterGuid;
	if (IsHttpBackendServerRuntime() || !HasAccessToken()
		|| !FRPGBackendPlayerAuthState::IsSteamId(ExpectedSteamId)
		|| ExpectedSteamId != GetAuthenticatedSteamId()
		|| !FGuid::ParseExact(DungeonSessionId, EGuidFormats::DigitsWithHyphens, SessionGuid) || !SessionGuid.IsValid()
		|| !FGuid::ParseExact(CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid())
	{
		OnDungeonAbandonmentCompleted.Broadcast(Identity, false, TEXT("dungeon_abandonment_authentication_required"));
		return;
	}
	FHttpRequestRef Request = CreateDungeonAbandonmentRequest(DungeonSessionId, CharacterId, ExpectedSteamId);
	if (!Request->ProcessRequest())
		OnAbandonDungeonSessionResponse(Request, nullptr, false, DungeonSessionId, CharacterId,
			ExpectedSteamId, PlayerDungeonRequestGeneration);
}

FHttpRequestRef UHttpWebManager::CreateDungeonAbandonmentRequest(const FString& SessionId,
	const FString& CharacterId, const FString& ExpectedSteamId)
{
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(this, &ThisClass::OnAbandonDungeonSessionResponse,
		SessionId, CharacterId, ExpectedSteamId, PlayerDungeonRequestGeneration);
	Request->SetURL(ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/members/%s/abandon"),
		*FGenericPlatformHttp::UrlEncode(SessionId), *FGenericPlatformHttp::UrlEncode(CharacterId)));
	Request->SetVerb(TEXT("POST"));
	// Never fall back to a server/admin credential for this player mutation.
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), PlayerAuth.Authorization(FPlatformTime::Seconds()));
	Request->SetTimeout(RequestTimeoutSeconds);
	return Request;
}

void UHttpWebManager::OnAbandonDungeonSessionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, FString SessionId, FString CharacterId, FString ExpectedSteamId, uint64 RequestGeneration)
{
	// Discard stale callbacks BEFORE handling a 401; an old response cannot erase a renewed token.
	if (IsHttpBackendServerRuntime() || RequestGeneration != PlayerDungeonRequestGeneration) return;
	FRPGDungeonAbandonmentReceipt Receipt;
	Receipt.DungeonSessionId = SessionId;
	Receipt.CharacterId = CharacterId;
	const bool bAccountMatches = ExpectedSteamId == GetAuthenticatedSteamId() && !ExpectedSteamId.IsEmpty();
	const bool bAccepted = bAccountMatches && AcceptPlayerResponse(Request, Response, bWasSuccessful);
	if (!bAccepted)
	{
		OnDungeonAbandonmentCompleted.Broadcast(Receipt, false, TEXT("dungeon_abandonment_authentication_changed"));
		return;
	}
	const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
	FRPGDungeonAbandonmentReceipt Parsed;
	const bool bSuccess = bWasSuccessful && Response.IsValid()
		&& FRPGDungeonAbandonmentCodec::Decode(Response->GetContentAsString(), Status, SessionId, CharacterId, Parsed);
	FString Error = TEXT("dungeon_abandonment_unconfirmed");
	// Only this exact authenticated conflict is definitive. Timeouts/401/403/404/malformed
	// receipts do not prove whether an earlier attempt committed, so keep the retry identity.
	if (bWasSuccessful && Status == 409)
	{
		TSharedPtr<FJsonObject> Object;
		FString BackendError;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Object)
			&& Object.IsValid() && Object->TryGetStringField(TEXT("error"), BackendError)
			&& BackendError.Equals(TEXT("dungeon_abandonment_not_available"), ESearchCase::CaseSensitive)) Error = BackendError;
	}
	if (bSuccess) Receipt = MoveTemp(Parsed);
	OnDungeonAbandonmentCompleted.Broadcast(Receipt, bSuccess, bSuccess ? FString() : Error);
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
	if (ActiveDungeonSettlementRequest.IsValid())
	{
		ActiveDungeonSettlementRequest->OnProcessRequestComplete().Unbind();
		ActiveDungeonSettlementRequest->CancelRequest();
		ActiveDungeonSettlementRequest.Reset();
	}
}

void UHttpWebManager::StartConfiguredDungeonSession()
{
	SendConfiguredDungeonSessionRequest(TEXT("start"));
}

void UHttpWebManager::HeartbeatConfiguredDungeonSession()
{
	SendConfiguredDungeonSessionRequest(TEXT("heartbeat"));
}

void UHttpWebManager::FinishConfiguredDungeonSession(bool bCleared)
{
	SendConfiguredDungeonSessionRequest(
		TEXT("finish"),
		bCleared ? TEXT("Cleared") : TEXT("Failed"));
}

void UHttpWebManager::SettleConfiguredDungeonRewards(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	// Keep the original result authoritative while it is still in flight,
	// including when a disconnect was observed after dispatch.
	if (ActiveDungeonSettlementRequest.IsValid()) return;
	if (!CanDispatchConfiguredDungeonConnectionMutation())
	{
		// A local transient deferral, not a backend rejection or a failed dungeon.
		OnDungeonRewardSettlementRequested.Broadcast(TEXT("WaitingForDisconnectionReports"), 425, false);
		return;
	}
	FHttpRequestPtr Request = CreateDungeonRewardSettlementRequest(RewardVersion, CurrencyChanges, ItemRewards);
	if (Request.IsValid() && Request->ProcessRequest()) return;
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		if (!ClaimDungeonSettlementCompletion(ActiveDungeonSettlementRequest, Request)) return;
	}
	OnDungeonRewardSettlementRequested.Broadcast(FString(), 0, false);
}

FHttpRequestPtr UHttpWebManager::CreateDungeonRewardSettlementRequest(
	const FString& RewardVersion, const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	const FString NormalizedRewardVersion = RewardVersion.TrimStartAndEnd();
	if (!CanDispatchConfiguredDungeonConnectionMutation()
		|| ActiveDungeonSettlementRequest.IsValid()
		|| !IsConfiguredForDungeonServer()
		|| NormalizedRewardVersion.IsEmpty()
		|| CurrencyChanges.Num() > 16
		|| ItemRewards.Num() > 16
		|| !FRPGDungeonItemReward::HasValidPersonalLootPool(ItemRewards))
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("serverId"), GameServerId);
	RootObject->SetStringField(
		TEXT("rewardVersion"),
		NormalizedRewardVersion);
	TArray<TSharedPtr<FJsonValue>> ChangeValues;
	ChangeValues.Reserve(CurrencyChanges.Num());
	for (const FRPGCurrencyChange& Change : CurrencyChanges)
	{
		const FString CurrencyCode = Change.CurrencyCode.ToString();
		if (CurrencyCode.IsEmpty() || Change.Delta <= 0)
		{
			return nullptr;
		}

		TSharedRef<FJsonObject> ChangeObject = MakeShared<FJsonObject>();
		ChangeObject->SetStringField(TEXT("currencyCode"), CurrencyCode);
		ChangeObject->SetField(
			TEXT("delta"),
			MakeShared<FJsonValueNumberString>(LexToString(Change.Delta)));
		ChangeValues.Add(MakeShared<FJsonValueObject>(ChangeObject));
	}
	RootObject->SetArrayField(TEXT("changes"), ChangeValues);

	TArray<TSharedPtr<FJsonValue>> ItemRewardValues;
	ItemRewardValues.Reserve(ItemRewards.Num());
	for (const FRPGDungeonItemReward& Reward : ItemRewards)
	{
		if (Reward.DefinitionType.IsNone()
			|| Reward.DefinitionName.IsNone()
			|| Reward.DefinitionVersion < 1
			|| Reward.Quantity < 1
			|| Reward.MaxStackSize < 0
			|| (Reward.MaxStackSize > 0 && Reward.Quantity > Reward.MaxStackSize)
			|| !Reward.Durability.IsValid())
		{
			return nullptr;
		}

		TSharedRef<FJsonObject> RewardObject = MakeShared<FJsonObject>();
		RewardObject->SetStringField(
			TEXT("definitionType"),
			Reward.DefinitionType.ToString());
		RewardObject->SetStringField(
			TEXT("definitionName"),
			Reward.DefinitionName.ToString());
		RewardObject->SetNumberField(
			TEXT("definitionVersion"),
			Reward.DefinitionVersion);
		RewardObject->SetNumberField(TEXT("quantity"), Reward.Quantity);
		RewardObject->SetNumberField(TEXT("maxStackSize"), Reward.MaxStackSize);
		RewardObject->SetStringField(
			TEXT("bindState"),
			ToBackendBindState(Reward.BindState));
		RewardObject->SetNumberField(
			TEXT("durabilityCurrent"),
			Reward.Durability.Current);
		RewardObject->SetNumberField(
			TEXT("durabilityMaximum"),
			Reward.Durability.Maximum);

		TArray<FGameplayTag> Tags;
		Reward.InstanceTags.GetGameplayTagArray(Tags);
		TArray<TSharedPtr<FJsonValue>> TagValues;
		TagValues.Reserve(Tags.Num());
		for (const FGameplayTag& Tag : Tags)
		{
			TagValues.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		RewardObject->SetArrayField(TEXT("instanceTags"), TagValues);

		TArray<TSharedPtr<FJsonValue>> StatValues;
		StatValues.Reserve(Reward.StatValues.Num());
		for (const FRPGDungeonItemRewardStat& Stat : Reward.StatValues)
		{
			if (!Stat.StatTag.IsValid() || !FMath::IsFinite(Stat.Value))
			{
				return nullptr;
			}

			TSharedRef<FJsonObject> StatObject = MakeShared<FJsonObject>();
			StatObject->SetStringField(
				TEXT("statTag"),
				Stat.StatTag.ToString());
			StatObject->SetNumberField(TEXT("value"), Stat.Value);
			StatValues.Add(MakeShared<FJsonValueObject>(StatObject));
		}
		RewardObject->SetArrayField(TEXT("statValues"), StatValues);
		if (!Reward.WriteEquipmentProgression(*RewardObject)
			|| !Reward.WritePersonalLootWeight(*RewardObject))
		{
			return nullptr;
		}
		ItemRewardValues.Add(MakeShared<FJsonValueObject>(RewardObject));
	}
	RootObject->SetArrayField(TEXT("itemRewards"), ItemRewardValues);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return nullptr;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	const FDungeonMutationScope Scope = CaptureDungeonMutationScope();
	ActiveDungeonSettlementRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Scope, NormalizedRewardVersion](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{
			if (!ClaimDungeonSettlementCompletion(ActiveDungeonSettlementRequest, Completed)) return;
			if (!IsCurrentDungeonMutationScope(Scope)) return;
			OnSettleDungeonRewardsResponseReceived(Completed, Response, bTransport, NormalizedRewardVersion);
		});
	Request->SetURL(ApiUrl / FString::Printf(
		TEXT("dungeon-sessions/%s/settle-rewards"),
		*FGenericPlatformHttp::UrlEncode(ConfiguredDungeonSessionId)));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	return Request;
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

bool UHttpWebManager::LoadConfiguredDungeonRewardSnapshot(const FString& CharacterId,
	TFunction<void(FRPGDungeonRewardSnapshotResult)> Completion, const FString& RewardVersion)
{
	FGuid CharacterGuid;
	if (!IsHttpBackendServerRuntime() || !IsConfiguredForDungeonServer()
		|| !FGuid::Parse(CharacterId, CharacterGuid) || !CharacterGuid.IsValid()) return false;
	const FString ExpectedCharacterId = CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString ExpectedSessionId = ConfiguredDungeonSessionId;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[ExpectedSessionId, ExpectedCharacterId, Completion = MoveTemp(Completion)]
		(FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess) mutable
		{
			FRPGDungeonRewardSnapshotResult Result;
			Result.HttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
			if (bSuccess && Response.IsValid())
				FRPGDungeonRewardSnapshotCodec::Decode(Response->GetContentAsString(), Result.HttpStatus,
					ExpectedSessionId, ExpectedCharacterId, Result);
			if (Completion) Completion(MoveTemp(Result));
		});
	FString SnapshotUrl = ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/reward-snapshot/%s"),
		*FGenericPlatformHttp::UrlEncode(ExpectedSessionId), *ExpectedCharacterId);
	if (!RewardVersion.IsEmpty()) SnapshotUrl += TEXT("?rewardVersion=") + FGenericPlatformHttp::UrlEncode(RewardVersion);
	Request->SetURL(SnapshotUrl);
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	return Request->ProcessRequest();
}

void UHttpWebManager::SendConfiguredDungeonSessionRequest(
	const FString& Action,
	const FString& Outcome)
{
	if (!IsHttpBackendServerRuntime()
		|| BackendServiceToken.IsEmpty()
		|| GameServerId.IsEmpty()
		|| ConfiguredDungeonSessionId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("serverId"), GameServerId);
	if (!Outcome.IsEmpty())
	{
		RootObject->SetStringField(TEXT("outcome"), Outcome);
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/%s"),
			*FGenericPlatformHttp::UrlEncode(ConfiguredDungeonSessionId),
			*Action),
		TEXT("POST"),
		RootObject,
		Action,
		ConfiguredDungeonSessionId);
}

void UHttpWebManager::SendDungeonSessionRequest(
	const FString& RelativePath,
	const FString& Verb,
	const TSharedPtr<FJsonObject>& RequestObject,
	const FString& Operation,
	const FString& ExpectedDungeonSessionId)
{
	FString RequestBody;
	if (RequestObject.IsValid())
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
			Writer = TJsonWriterFactory<
				TCHAR,
				TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
		if (!FJsonSerializer::Serialize(RequestObject.ToSharedRef(), Writer))
		{
			OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
			return;
		}
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnDungeonSessionResponseReceived,
		Operation,
		ExpectedDungeonSessionId,
		PlayerDungeonRequestGeneration);
	Request->SetURL(ApiUrl / RelativePath);
	Request->SetVerb(Verb);
	if (RequestObject.IsValid())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(RequestBody);
	}
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
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

void UHttpWebManager::OnAuthenticateResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActiveAuthenticationRequest) return;
	NotifyServiceDraining(Request, Response);
	const double RequestStartedSeconds = ActiveAuthenticationRequestStartedSeconds;
	ActiveAuthenticationRequest.Reset();
	ActiveAuthenticationRequestStartedSeconds = -1.0;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString NewAccessToken;
		FString SteamId;
		FString ExpiresAt;
		FString IssuedAt;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(TEXT("accessToken"), NewAccessToken)
			&& RootObject->TryGetStringField(TEXT("steamId"), SteamId)
			&& RootObject->TryGetStringField(TEXT("expiresAt"), ExpiresAt)
			&& RootObject->TryGetStringField(TEXT("issuedAt"), IssuedAt)
			&& PlayerAuth.AcceptLogin(NewAccessToken, SteamId, ExpiresAt, IssuedAt,
				RequestStartedSeconds, FPlatformTime::Seconds());
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Steam backend authentication failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnBackendAuthenticationCompleted.Broadcast(bSuccess);
}

bool UHttpWebManager::AcceptPlayerResponse(const FHttpRequestPtr& Request,
	const FHttpResponsePtr& Response, bool& bWasSuccessful)
{
	// Server callbacks share the dungeon transport, but use a distinct service credential.
	if (IsHttpBackendServerRuntime()) return true;
	if (!Request.IsValid()) return false;
	const FString Authorization = Request->GetHeader(TEXT("Authorization"));
	if (!PlayerAuth.MatchesRequest(Authorization))
	{
		// Expiry can fall between the operation guard and header construction. Complete that
		// unauthenticated request as failure, never applying its body or invalidating a new token.
		if (Authorization.IsEmpty() && !HasAccessToken())
		{
			bWasSuccessful = false;
			return true;
		}
		return false;
	}
	NotifyServiceDraining(Request, Response);
	PlayerAuth.InvalidateIfUnauthorized(Authorization,
		Response.IsValid() ? Response->GetResponseCode() : 0);
	// Concurrent requests must finish (releasing busy UI), but cannot apply data after expiry/401.
	if (!HasAccessToken()) bWasSuccessful = false;
	return true;
}

void UHttpWebManager::OnLoadCharactersResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	TArray<FRPGBackendCharacter> Characters;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TArray<TSharedPtr<FJsonValue>> CharacterValues;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, CharacterValues);
		if (bSuccess)
		{
			Characters.Reserve(CharacterValues.Num());
			for (const TSharedPtr<FJsonValue>& CharacterValue : CharacterValues)
			{
				FRPGBackendCharacter Character;
				if (!CharacterValue.IsValid()
					|| CharacterValue->Type != EJson::Object
					|| !FRPGBackendCharacter::Decode(
						CharacterValue->AsObject(),
						Character))
				{
					bSuccess = false;
					Characters.Reset();
					break;
				}
				Characters.Add(MoveTemp(Character));
			}
		}
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character list request failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharactersLoaded.Broadcast(Characters, bSuccess);
}

void UHttpWebManager::OnCreateCharacterResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	FRPGBackendCharacter Character;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& FRPGBackendCharacter::Decode(RootObject, Character);
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character creation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharacterCreated.Broadcast(Character, bSuccess);
}

void UHttpWebManager::OnDungeonSessionResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString Operation,
	FString ExpectedDungeonSessionId,
	uint64 RequestGeneration)
{
	if (!IsHttpBackendServerRuntime() && RequestGeneration != PlayerDungeonRequestGeneration) return;
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	FRPGDungeonSession DungeonSession;
	const int32 ResponseCode = Response.IsValid()
		? Response->GetResponseCode()
		: 0;
	const bool bNoActiveSession = bWasSuccessful
		&& ResponseCode == EHttpResponseCodes::NoContent
		&& Operation.Equals(TEXT("resume"), ESearchCase::CaseSensitive);
	bool bSuccess = bNoActiveSession
		|| (bWasSuccessful && Response.IsValid()
			&& EHttpResponseCodes::IsOk(ResponseCode));

	if (bSuccess && !bNoActiveSession)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& TryParseDungeonSession(RootObject, DungeonSession)
			&& (ExpectedDungeonSessionId.IsEmpty()
				|| DungeonSession.DungeonSessionId.Equals(
					ExpectedDungeonSessionId,
					ESearchCase::CaseSensitive));
	}

	if (!bSuccess)
	{
		DungeonSession = FRPGDungeonSession();
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon session %s request failed (HTTP %d)."),
			*Operation,
			ResponseCode);
	}

	OnDungeonSessionUpdated.Broadcast(DungeonSession, bSuccess);
}

void UHttpWebManager::OnSettleDungeonRewardsResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString ExpectedRewardVersion)
{
	const int32 ResponseCode = Response.IsValid()
		? Response->GetResponseCode()
		: 0;
	bool bSuccess = bWasSuccessful
		&& Response.IsValid()
		&& EHttpResponseCodes::IsOk(ResponseCode);
	FString State;
	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString DungeonSessionId;
		FString RewardVersion;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(
				TEXT("dungeonSessionId"),
				DungeonSessionId)
			&& RootObject->TryGetStringField(TEXT("state"), State)
			&& RootObject->TryGetStringField(
				TEXT("rewardVersion"),
				RewardVersion)
			&& DungeonSessionId.Equals(
				ConfiguredDungeonSessionId,
				ESearchCase::CaseSensitive)
			&& RewardVersion.Equals(
				ExpectedRewardVersion,
				ESearchCase::CaseSensitive);
	}

	if (!bSuccess)
	{
		State.Reset();
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon reward settlement request failed (HTTP %d)."),
			ResponseCode);
	}
	OnDungeonRewardSettlementRequested.Broadcast(
		State,
		ResponseCode,
		bSuccess);
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
