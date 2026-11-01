#include "Manager/HttpWebManager.h"
#include "HAL/PlatformTime.h"
#include "Economy/RPGDungeonRewardSnapshot.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "Online/RPGJoinTicketAdmission.h"
#include "Online/RPGDungeonDisconnectionJournal.h"
#include "Online/RPGDungeonConsumeJournal.h"

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
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/SubsystemCollection.h"
#include "TimerManager.h"

THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
	bool ClaimDungeonConsumeCompletion(TMap<FString, FHttpRequestPtr>& Requests,
		const FString& Ticket, const FHttpRequestPtr& Completed)
	{
		const FHttpRequestPtr* Current = Requests.Find(Ticket);
		if (!Current || *Current != Completed) return false;
		Requests.Remove(Ticket);
		return true;
	}

	bool ClaimDungeonSettlementCompletion(FHttpRequestPtr& Active, const FHttpRequestPtr& Completed)
	{
		if (!Active.IsValid() || Active != Completed) return false;
		Active.Reset();
		return true;
	}

	bool IsDefinitiveDungeonConsumeDenial(const FHttpResponsePtr& Response)
	{
		if (!Response.IsValid() || Response->GetResponseCode() != 401 || Response->GetContentLength() > 4096) return false;
		TSharedPtr<FJsonObject> Error;
		FString Code;
		return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Error)
			&& Error.IsValid() && Error->Values.Num() == 1 && Error->TryGetStringField(TEXT("error"), Code)
			&& Code.Equals(TEXT("join_ticket_not_recoverable"), ESearchCase::CaseSensitive);
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

void UHttpWebManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bDungeonMembershipTransportShutdown = false;
	bDungeonDisconnectionReportShutdown = false;
	bDungeonDisconnectionPersistenceFailed = false;
	bDungeonConsumeRecoveryCompleting = false;
	bDungeonConsumePersistenceFailed = false;
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
		if (IsConfiguredForDungeonServer()) InitializeDungeonDisconnectionReports();
	}
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
	bDungeonDisconnectionReportShutdown = true;
	CancelDungeonConnectionMutations();
	FTSTicker::GetCoreTicker().RemoveTicker(DungeonDisconnectionTicker);
	// Removed ticker elements may live until the next frame; release our handle now.
	DungeonDisconnectionTicker.Reset();
	for (auto& Entry : DungeonDisconnectionReports)
	{
		if (!Entry.Value->Request.IsValid()) continue;
		Entry.Value->Request->OnProcessRequestComplete().Unbind();
		Entry.Value->Request->CancelRequest();
		Entry.Value->Request.Reset();
	}
	DungeonDisconnectionReports.Reset();
	DungeonDisconnectionJournal.Reset();
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

void UHttpWebManager::ConsumeJoinTicket(const FString& JoinTicket, const FString& ExpectedSteamId)
{
	// A duplicate caller must not fail the original pending admission.
	if (ActiveDungeonConsumeRequests.Contains(JoinTicket) || DungeonConsumeRecoveries.Contains(JoinTicket)) return;
	FHttpRequestPtr Request = CreateConsumeJoinTicketRequest(JoinTicket, ExpectedSteamId);
	if (!Request.IsValid())
	{
		OnJoinTicketConsumed.Broadcast(JoinTicket, FString(), FString(), FString(), 0, false);
		return;
	}
	DispatchDungeonConsumeAttempt(JoinTicket, Request);
}

void UHttpWebManager::DispatchDungeonConsumeAttempt(const FString& JoinTicket, const FHttpRequestPtr& Request)
{
	if (Request.IsValid() && Request->ProcessRequest()) return;
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		// ProcessRequest(false) may already have synchronously completed on this thread.
		if (!ClaimDungeonConsumeCompletion(ActiveDungeonConsumeRequests, JoinTicket, Request)) return;
	}
	OnConsumeJoinTicketResponseReceived(Request, nullptr, false, JoinTicket);
}

UHttpWebManager::FDungeonMutationScope UHttpWebManager::CaptureDungeonMutationScope() const
{
	FDungeonMutationScope Scope;
	Scope.BackendUrl = ApiUrl; Scope.ServerId = GameServerId; Scope.SessionId = ConfiguredDungeonSessionId;
	Scope.World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return Scope;
}

bool UHttpWebManager::IsCurrentDungeonMutationScope(const FDungeonMutationScope& Scope) const
{
	UWorld* CurrentWorld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return IsInGameThread() && !bDungeonDisconnectionReportShutdown && !Scope.World.IsStale(true)
		&& Scope.World.Get() == CurrentWorld && (!CurrentWorld || !CurrentWorld->bIsTearingDown)
		&& Scope.BackendUrl.Equals(ApiUrl, ESearchCase::CaseSensitive)
		&& Scope.ServerId.Equals(GameServerId, ESearchCase::CaseSensitive)
		&& Scope.SessionId.Equals(ConfiguredDungeonSessionId, ESearchCase::CaseSensitive);
}

void UHttpWebManager::CancelDungeonConnectionMutations()
{
	for (auto& Entry : ActiveDungeonConsumeRequests)
	{
		Entry.Value->OnProcessRequestComplete().Unbind();
		Entry.Value->CancelRequest();
	}
	ActiveDungeonConsumeRequests.Reset();
	for (auto& Entry : DungeonConsumeRecoveries)
	{
		if (!Entry.Value->EvidenceRequest.IsValid()) continue;
		Entry.Value->EvidenceRequest->OnProcessRequestComplete().Unbind();
		Entry.Value->EvidenceRequest->CancelRequest();
		Entry.Value->EvidenceRequest.Reset();
	}
	DungeonConsumeRecoveries.Reset();
	if (ActiveDungeonSettlementRequest.IsValid())
	{
		ActiveDungeonSettlementRequest->OnProcessRequestComplete().Unbind();
		ActiveDungeonSettlementRequest->CancelRequest();
		ActiveDungeonSettlementRequest.Reset();
	}
}

bool UHttpWebManager::CanDispatchConfiguredDungeonConnectionMutation() const
{
	if (!IsDungeonDisconnectionOutboxReady() || bDungeonConsumeRecoveryCompleting || bDungeonConsumePersistenceFailed
		|| !PendingDungeonConsumeObservationWrites.IsEmpty()
		|| !DungeonConsumeJournal || !DungeonConsumeJournal->IsHealthy() || !DungeonConsumeJournal->GetPending().IsEmpty()) return false;
	for (const auto& Entry : DungeonConsumeRecoveries)
		if (Entry.Value->bUncertain) return false;
	return true;
}

bool UHttpWebManager::IsDungeonDisconnectionOutboxReady() const
{
	return IsInGameThread() && IsHttpBackendServerRuntime() && IsConfiguredForDungeonServer()
		&& !bDungeonDisconnectionReportShutdown && !bDungeonDisconnectionPersistenceFailed
		&& !bDungeonDisconnectionRestorePending && !bDungeonConsumeRestorePending
		&& PendingDungeonDisconnectionWrites.IsEmpty()
		&& DungeonDisconnectionJournal && DungeonDisconnectionJournal->IsHealthy()
		&& DungeonDisconnectionJournal->GetPending().IsEmpty() && DungeonDisconnectionReports.IsEmpty();
}

FString UHttpWebManager::BuildConfiguredDungeonRecoveryDiagnostics(int32 MaxIntents) const
{
	// No transport, file enumeration, restore/bind, retry or cleanup from this path.
	if (!IsInGameThread() || !IsHttpBackendServerRuntime())
		return TEXT("{\"available\":false,\"reason\":\"server_game_thread_required\"}");
	if (bDungeonDisconnectionReportShutdown)
		return TEXT("{\"available\":false,\"reason\":\"shutdown\"}");
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (World && (World->GetNetMode() == NM_Client || World->bIsTearingDown))
		return TEXT("{\"available\":false,\"reason\":\"server_world_required\"}");
	auto SafeUuid = [](const FString& Value)
	{
		FGuid Id;
		return FGuid::Parse(Value, Id) && Id.IsValid()
			? Id.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	};
	const auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetBoolField(TEXT("available"), true);
	Root->SetStringField(TEXT("dungeonSessionId"), SafeUuid(ConfiguredDungeonSessionId));
	Root->SetBoolField(TEXT("configured"), IsConfiguredForDungeonServer());
	Root->SetBoolField(TEXT("credentialPresent"), !BackendServiceToken.IsEmpty());
	Root->SetBoolField(TEXT("mutationsAllowed"), CanDispatchConfiguredDungeonConnectionMutation());
	Root->SetBoolField(TEXT("completionInProgress"), bDungeonConsumeRecoveryCompleting);
	Root->SetNumberField(TEXT("activeConsumes"), ActiveDungeonConsumeRequests.Num());
	Root->SetBoolField(TEXT("activeSettlement"), ActiveDungeonSettlementRequest.IsValid());
	auto JournalStatus = [](bool Initialized, bool Healthy, bool RestorePending, int32 KnownRecords, int32 Writes, bool Sticky)
	{
		const auto Value = MakeShared<FJsonObject>();
		Value->SetBoolField(TEXT("initialized"), Initialized);
		Value->SetBoolField(TEXT("healthy"), Healthy);
		Value->SetBoolField(TEXT("restorePending"), RestorePending);
		Value->SetNumberField(TEXT("knownDurableRecords"), KnownRecords); // Cached knowledge, not a fresh disk assertion.
		Value->SetNumberField(TEXT("pendingWrites"), Writes);
		Value->SetBoolField(TEXT("unattributedFailure"), Sticky);
		return Value;
	};
	Root->SetObjectField(TEXT("consumeJournal"), JournalStatus(DungeonConsumeJournal.IsValid(),
		DungeonConsumeJournal && DungeonConsumeJournal->IsHealthy(), bDungeonConsumeRestorePending,
		DungeonConsumeJournal ? DungeonConsumeJournal->GetPending().Num() : 0,
		PendingDungeonConsumeObservationWrites.Num(), bDungeonConsumePersistenceFailed));
	const auto Outbox = JournalStatus(DungeonDisconnectionJournal.IsValid(),
		DungeonDisconnectionJournal && DungeonDisconnectionJournal->IsHealthy(), bDungeonDisconnectionRestorePending,
		DungeonDisconnectionJournal ? DungeonDisconnectionJournal->GetPending().Num() : 0,
		PendingDungeonDisconnectionWrites.Num(), bDungeonDisconnectionPersistenceFailed);
	Outbox->SetNumberField(TEXT("queuedReports"), DungeonDisconnectionReports.Num());
	Root->SetObjectField(TEXT("disconnectionOutbox"), Outbox);
	TMap<FString, TSharedPtr<FDungeonConsumeRecovery>> States;
	TSet<FString> ActiveIds;
	int32 ConflictingOwners = 0;
	auto Collect = [&States, &ConflictingOwners](const TSharedPtr<FDungeonConsumeRecovery>& State)
	{
		if (!State) return;
		if (const auto* Previous = States.Find(State->RequestId); Previous && *Previous != State) ++ConflictingOwners;
		else States.Add(State->RequestId, State);
	};
	for (const auto& Entry : DungeonConsumeRecoveries)
	{
		Collect(Entry.Value);
		if (ActiveDungeonConsumeRequests.Contains(Entry.Key)) ActiveIds.Add(Entry.Value->RequestId);
	}
	for (const auto& Entry : PendingDungeonConsumeObservationWrites) Collect(Entry.Value);
	if (DungeonConsumeJournal)
		for (const auto& Entry : DungeonConsumeJournal->GetPending())
			if (!States.Contains(Entry.Key)) States.Add(Entry.Key, nullptr);
	TArray<FString> Ids; States.GenerateKeyArray(Ids); Ids.Sort();
	Root->SetNumberField(TEXT("totalIntents"), Ids.Num());
	Root->SetNumberField(TEXT("conflictingOwners"), ConflictingOwners);
	TArray<TSharedPtr<FJsonValue>> Intents;
	const double Now = FPlatformTime::Seconds();
	const int32 Limit = FMath::Clamp(MaxIntents, 1, 64);
	for (const FString& Id : Ids)
	{
		if (Intents.Num() >= Limit) break;
		const auto State = States.FindRef(Id);
		const auto Value = MakeShared<FJsonObject>();
		Value->SetStringField(TEXT("consumptionRequestId"), SafeUuid(Id));
		Value->SetBoolField(TEXT("durableObservationKnown"), DungeonConsumeJournal && DungeonConsumeJournal->GetPending().Contains(Id));
		Value->SetBoolField(TEXT("observationWritePending"), PendingDungeonConsumeObservationWrites.Contains(Id));
		const FRPGDungeonConsumeLossRecord* Record = DungeonConsumeJournal ? DungeonConsumeJournal->GetPending().Find(Id) : nullptr;
		const FDateTime Observed = State ? State->LostAtUtc : Record ? Record->ObservedAtUtc : FDateTime();
		Value->SetStringField(TEXT("observedAtUtc"), Observed.GetTicks() > 0 ? Observed.ToIso8601() : FString());
		const TCHAR* Stage = TEXT("durable_record_unbound");
		const TCHAR* Outcome = TEXT("not_checked");
		bool Review = false;
		if (State)
		{
			const bool Current = !State->bRestoredWorldPending && IsCurrentDungeonMutationScope(State->Scope);
			Value->SetBoolField(TEXT("scopeCurrent"), Current);
			Value->SetBoolField(TEXT("expired"), State->bExpired);
			Value->SetBoolField(TEXT("uncertain"), State->bUncertain);
			Value->SetStringField(TEXT("knownConnectionGeneration"), State->EvidenceGeneration > 0 ? LexToString(State->EvidenceGeneration) : FString());
			Value->SetStringField(TEXT("lastOperation"), State->LastEvidenceOutcome == FDungeonConsumeRecovery::EEvidenceOutcome::NotChecked
				? TEXT("none") : State->bLastEvidenceWasSeal ? TEXT("seal") : TEXT("evidence"));
			Value->SetNumberField(TEXT("lastHttpStatus"), State->LastEvidenceHttpStatus);
			Value->SetStringField(TEXT("lastCompletedAtUtc"), State->LastEvidenceCompletedAtUtc.GetTicks() > 0
				? State->LastEvidenceCompletedAtUtc.ToIso8601() : FString());
			Value->SetNumberField(TEXT("retryAfterSeconds"), FMath::IsFinite(State->NextAttemptAt)
				? FMath::Clamp(State->NextAttemptAt - Now, 0.0, 86400.0) : 0.0);
			switch (State->LastEvidenceOutcome)
			{
			case FDungeonConsumeRecovery::EEvidenceOutcome::TransportFailure: Outcome = TEXT("transport_failure"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::HttpRejected: Outcome = TEXT("http_rejected"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::InvalidResponse: Outcome = TEXT("invalid_response"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved: Outcome = TEXT("backend_unresolved"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::ConsumptionRecorded: Outcome = TEXT("consumption_recorded"); break;
			case FDungeonConsumeRecovery::EEvidenceOutcome::CancellationVerified: Outcome = TEXT("cancellation_verified"); break;
			default: break;
			}
			Review = State->LastEvidenceOutcome == FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved && State->bLastEvidenceWasSeal;
			if (PendingDungeonConsumeObservationWrites.Contains(Id)) Stage = TEXT("observation_write_pending");
			else if (State->bRestoredWorldPending) Stage = TEXT("awaiting_gameplay_world");
			else if (!Current) Stage = TEXT("scope_inactive");
			else if (State->bCancellationVerified) Stage = TEXT("cancellation_cleanup_pending");
			else if (State->EvidenceGeneration > 0) Stage = TEXT("disconnection_handoff_pending");
			else if (State->bExpired && State->LostAtUtc.GetTicks() <= 0) Stage = TEXT("loss_not_observed");
			else if (State->EvidenceRequest.IsValid()) Stage = State->bSealRequested ? TEXT("seal_in_flight") : TEXT("evidence_in_flight");
			else if (State->bExpired) Stage = State->bSealRequested ? TEXT("seal_retry_pending") : TEXT("evidence_retry_pending");
			else if (ActiveIds.Contains(Id)) Stage = TEXT("consume_in_flight");
			else Stage = TEXT("consume_retry_pending");
		}
		Value->SetStringField(TEXT("stage"), Stage);
		Value->SetStringField(TEXT("lastOutcome"), Outcome);
		Value->SetBoolField(TEXT("operatorReviewSuggested"), Review);
		Intents.Add(MakeShared<FJsonValueObject>(Value));
	}
	Root->SetArrayField(TEXT("intents"), Intents);
	Root->SetNumberField(TEXT("omittedIntents"), Ids.Num() - Intents.Num());
	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
	return Json;
}

FHttpRequestPtr UHttpWebManager::CreateConsumeJoinTicketRequest(const FString& JoinTicket, const FString& ExpectedSteamId)
{
	if (!CanDispatchConfiguredDungeonConnectionMutation()
		|| ActiveDungeonConsumeRequests.Contains(JoinTicket)
		|| DungeonConsumeRecoveries.Contains(JoinTicket)
		|| JoinTicket.IsEmpty() || JoinTicket.Len() > 256
		|| BackendServiceToken.IsEmpty()
		|| GameServerId.IsEmpty()
		|| ConfiguredDungeonSessionId.IsEmpty())
	{
		return nullptr;
	}

	const auto Recovery = MakeShared<FDungeonConsumeRecovery>();
	Recovery->Scope = CaptureDungeonMutationScope();
	Recovery->RequestId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	Recovery->Deadline = FPlatformTime::Seconds() + 600.0;
	// Bind recovery to the original peer BEFORE any backend reply. A missing dev
	// identity may still use ordinary admission, but cannot resolve historical evidence.
	Recovery->ExpectedSteamId = ExpectedSteamId;
	// The consume endpoint hashes its trimmed token; the evidence key must match it.
	const FTCHARToUTF8 TicketUtf8(*JoinTicket.TrimStartAndEnd());
	uint8 Digest[SHA256_DIGEST_LENGTH];
	if (!SHA256(reinterpret_cast<const uint8*>(TicketUtf8.Get()), TicketUtf8.Length(), Digest)) return nullptr;
	Recovery->TicketHash = BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("joinTicket"), JoinTicket);
	RootObject->SetStringField(TEXT("serverId"), Recovery->Scope.ServerId);
	RootObject->SetStringField(TEXT("consumptionRequestId"), Recovery->RequestId);

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&Recovery->Body);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		return nullptr;
	}
	DungeonConsumeRecoveries.Add(JoinTicket, Recovery);
	FHttpRequestPtr Request = CreateDungeonConsumeAttempt(JoinTicket, Recovery);
	if (!Request.IsValid()) DungeonConsumeRecoveries.Remove(JoinTicket);
	return Request;
}

FHttpRequestPtr UHttpWebManager::CreateDungeonConsumeAttempt(
	const FString& JoinTicket, const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(JoinTicket) != Recovery
		|| Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || !IsDungeonDisconnectionOutboxReady()
		|| ActiveDungeonConsumeRequests.Contains(JoinTicket) || BackendServiceToken.IsEmpty()) return nullptr;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	ActiveDungeonConsumeRequests.Add(JoinTicket, Request);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Recovery, JoinTicket](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{
			if (!ClaimDungeonConsumeCompletion(ActiveDungeonConsumeRequests, JoinTicket, Completed)) return;
			if (DungeonConsumeRecoveries.FindRef(JoinTicket) != Recovery) return;
			// A newly queued loss blocks future sends, not an already committed response.
			OnConsumeJoinTicketResponseReceived(Completed, Response, bTransport, JoinTicket);
		});
	Request->SetURL(Recovery->Scope.BackendUrl / TEXT("join-tickets/consume"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(Recovery->Body);
	Request->SetTimeout(RequestTimeoutSeconds);
	return Request;
}

void UHttpWebManager::ObserveDungeonConsumeConnectionLoss(const FString& JoinTicket,
	const FString& ExpectedSteamId, FDateTime ObservedAtUtc)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	if (!Recovery || !IsCurrentDungeonMutationScope(Recovery->Scope) || ObservedAtUtc.GetTicks() <= 0
		|| Recovery->ExpectedSteamId.IsEmpty()
		|| !Recovery->ExpectedSteamId.Equals(ExpectedSteamId, ESearchCase::CaseSensitive)) return;
	if (Recovery->LostAtUtc.GetTicks() == 0) Recovery->LostAtUtc = ObservedAtUtc;
	PersistDungeonConsumeObservation(Recovery.ToSharedRef());
}

void UHttpWebManager::PersistDungeonConsumeObservation(const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (!IsCurrentDungeonMutationScope(Recovery->Scope)) return;
	const FRPGDungeonConsumeLossRecord Record {Recovery->RequestId, Recovery->TicketHash,
		Recovery->ExpectedSteamId, Recovery->LostAtUtc};
	if (const auto Existing = PendingDungeonConsumeObservationWrites.FindRef(Recovery->RequestId); Existing && Existing != Recovery)
	{
		// Never overwrite an earlier failed intent under a colliding UUID.
		bDungeonConsumePersistenceFailed = true;
		return;
	}
	if (!bDungeonConsumeRestorePending && DungeonConsumeJournal && DungeonConsumeJournal->MatchesAssignment(
		Recovery->Scope.BackendUrl, Recovery->Scope.ServerId, Recovery->Scope.SessionId) && DungeonConsumeJournal->Persist(Record))
	{
		PendingDungeonConsumeObservationWrites.Remove(Recovery->RequestId);
		return;
	}
	if (PendingDungeonConsumeObservationWrites.IsEmpty())
	{
		DungeonConsumeObservationWriteRetries = 0;
		NextDungeonConsumeObservationWriteAt = FPlatformTime::Seconds() + 1.0;
	}
	// The original first UTC/peer/hash live on this exact intent even if an old
	// HTTP callback is later cancelled. Never log/persist its in-memory ticket body.
	PendingDungeonConsumeObservationWrites.Add(Recovery->RequestId, Recovery);
}

void UHttpWebManager::RetryDungeonConsumeObservationWrites(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting || bDungeonConsumeRestorePending
		|| PendingDungeonConsumeObservationWrites.IsEmpty() || Now < NextDungeonConsumeObservationWriteAt) return;
	NextDungeonConsumeObservationWriteAt = Now + (DungeonConsumeObservationWriteRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> WriteGuard(bDungeonConsumeRecoveryCompleting, true);
	if (!DungeonConsumeJournal) return;
	TArray<FString> Keys; PendingDungeonConsumeObservationWrites.GenerateKeyArray(Keys);
	int32 Attempts = 0;
	bool bStorageChecked = false;
	// Rotate bounded batches so repeatedly failing records cannot starve other
	// original observations queued before the mutation barrier closed.
	const int32 Offset = static_cast<int32>((static_cast<int64>(DungeonConsumeObservationWriteRetries - 1) * 4) % Keys.Num());
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FString& Key = Keys[(Offset + Index) % Keys.Num()];
		const auto Recovery = PendingDungeonConsumeObservationWrites.FindRef(Key);
		if (!Recovery || Recovery->bRestoredWorldPending || !IsCurrentDungeonMutationScope(Recovery->Scope)
			|| !DungeonConsumeJournal->MatchesAssignment(Recovery->Scope.BackendUrl, Recovery->Scope.ServerId, Recovery->Scope.SessionId)) continue;
		if (!bStorageChecked)
		{
			// Revalidate the entire directory and all previously durable entries.
			// Partial/corrupt/conflicting files are retained, never "repaired" by deletion.
			if (!DungeonConsumeJournal->IsHealthy() && !DungeonConsumeJournal->RecoverForRetry()) return;
			bStorageChecked = true;
		}
		PersistDungeonConsumeObservation(Recovery.ToSharedRef());
		if (++Attempts >= 4) break;
	}
}

bool UHttpWebManager::HasExpiredDungeonConsumeRecovery(const FString& JoinTicket) const
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	return Recovery && Recovery->bExpired && IsCurrentDungeonMutationScope(Recovery->Scope);
}

void UHttpWebManager::ExpireDungeonConsumeRecovery(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (Recovery->bExpired || DungeonConsumeRecoveries.FindRef(Ticket) != Recovery) return;
	Recovery->bExpired = true;
	Recovery->bUncertain = true;
	Recovery->bWaiting = false;
	Recovery->NextAttemptAt = 0.0;
	UE_LOG(LogTemp, Warning, TEXT("Dungeon admission recovery expired; retaining the original intent for evidence reconciliation."));
	// Exactly one engine failure; historical evidence must NEVER broadcast a success.
	OnJoinTicketConsumed.Broadcast(Ticket, FString(), FString(), FString(), 0, false);
}

FHttpRequestPtr UHttpWebManager::TakeDueDungeonConsumeRetry(const FString& Ticket, double Now)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (!Recovery || !Recovery->bWaiting || ActiveDungeonConsumeRequests.Contains(Ticket)) return nullptr;
	if (!IsCurrentDungeonMutationScope(Recovery->Scope))
	{
		DungeonConsumeRecoveries.Remove(Ticket);
		return nullptr;
	}
	if (Now >= Recovery->Deadline)
	{
		ExpireDungeonConsumeRecovery(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Now < Recovery->NextAttemptAt) return nullptr;
	FHttpRequestPtr Request = CreateDungeonConsumeAttempt(Ticket, Recovery.ToSharedRef());
	if (Request.IsValid()) Recovery->bWaiting = false;
	else Recovery->NextAttemptAt = Now + 1.0;
	return Request;
}

FHttpRequestPtr UHttpWebManager::TakeDueDungeonConsumeEvidence(const FString& Ticket, double Now)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (bDungeonConsumeRestorePending || !Recovery || !Recovery->bExpired || Recovery->bRestoredWorldPending || Recovery->EvidenceRequest.IsValid()
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || Now < Recovery->NextAttemptAt
		|| Recovery->LostAtUtc.GetTicks() <= 0 || BackendServiceToken.IsEmpty()) return nullptr;
	if (Recovery->bCancellationVerified)
	{
		ResolveCancelledDungeonConsumeRecovery(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Recovery->EvidenceGeneration > 0)
	{
		QueueDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef());
		return nullptr;
	}
	if (Recovery->ExpectedSteamId.IsEmpty() || Recovery->ExpectedSteamId.Len() > 20) return nullptr;
	for (const TCHAR Character : Recovery->ExpectedSteamId)
		if (Character < TEXT('0') || Character > TEXT('9')) return nullptr;
	if (Recovery->EvidenceBody.IsEmpty())
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("dungeonSessionId"), Recovery->Scope.SessionId);
		Body->SetStringField(TEXT("serverId"), Recovery->Scope.ServerId);
		Body->SetStringField(TEXT("consumptionRequestId"), Recovery->RequestId);
		Body->SetStringField(TEXT("ticketHash"), Recovery->TicketHash);
		Body->SetStringField(TEXT("expectedSteamId"), Recovery->ExpectedSteamId);
		if (!FJsonSerializer::Serialize(Body,
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Recovery->EvidenceBody))) return nullptr;
	}
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Recovery->EvidenceRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Ticket, Recovery](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{ CompleteDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef(), Completed, Response, bTransport); });
	Request->SetURL(Recovery->Scope.BackendUrl / (Recovery->bSealRequested
		? TEXT("join-tickets/seal-consumption") : TEXT("join-tickets/consumption-evidence")));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(Recovery->EvidenceBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	return Request;
}

void UHttpWebManager::DispatchDungeonConsumeEvidence(const FString& Ticket, const FHttpRequestPtr& Request)
{
	if (!Request.IsValid() || Request->ProcessRequest()) return;
	Request->OnProcessRequestComplete().Unbind();
	const auto Recovery = DungeonConsumeRecoveries.FindRef(Ticket);
	if (Recovery) CompleteDungeonConsumeEvidence(Ticket, Recovery.ToSharedRef(), Request, nullptr, false);
}

void UHttpWebManager::CompleteDungeonConsumeEvidence(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery, const FHttpRequestPtr& Request,
	const FHttpResponsePtr& Response, bool bTransport)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery
		|| !Recovery->EvidenceRequest.IsValid() || Recovery->EvidenceRequest != Request) return;
	Recovery->EvidenceRequest.Reset(); // Claim exactly once, including synchronous ProcessRequest(false).
	if (!IsCurrentDungeonMutationScope(Recovery->Scope)) return;
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	const bool bSeal = Request->GetURL().Equals(Recovery->Scope.BackendUrl / TEXT("join-tickets/seal-consumption"), ESearchCase::CaseSensitive);
	Recovery->bLastEvidenceWasSeal = bSeal;
	Recovery->LastEvidenceCompletedAtUtc = FDateTime::UtcNow(); // Not an observed socket-loss timestamp.
	Recovery->LastEvidenceHttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
	Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::InvalidResponse;
	if (!bTransport || !Response.IsValid())
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::TransportFailure;
		return;
	}
	if (Response->GetContentLength() > 8192) return;
	if (Response->GetResponseCode() == 409)
	{
		TSharedPtr<FJsonObject> Error;
		FString Code;
		// Only the authenticated endpoint's exact marker requests sealing. A
		// generic conflict, missing record or HTTP denial never releases a guard.
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Error)
			&& Error && Error->Values.Num() == 1 && Error->TryGetStringField(TEXT("error"), Code)
			&& Code.Equals(TEXT("join_ticket_consumption_unresolved"), ESearchCase::CaseSensitive))
		{
			Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::Unresolved;
			if (!bSeal) Recovery->bSealRequested = true;
		}
		return;
	}
	if (Response->GetResponseCode() != 200)
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::HttpRejected;
		return;
	}
	FRPGJoinTicketConsumptionCancellation Cancellation;
	if (bSeal && Recovery->EvidenceGeneration == 0
		&& FRPGJoinTicketConsumptionCancellation::Decode(Response->GetContentAsString(), Cancellation)
		&& Cancellation.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive)
		&& Cancellation.ServerId.Equals(Recovery->Scope.ServerId, ESearchCase::CaseSensitive)
		&& Cancellation.ConsumptionRequestId.Equals(Recovery->RequestId, ESearchCase::CaseSensitive)
		&& Cancellation.TicketHash.Equals(Recovery->TicketHash, ESearchCase::CaseSensitive)
		&& Cancellation.ExpectedSteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive))
	{
		Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::CancellationVerified;
		Recovery->bCancellationVerified = true;
		ResolveCancelledDungeonConsumeRecovery(Ticket, Recovery);
		return;
	}
	FRPGJoinTicketConsumptionEvidence Evidence;
	if (!FRPGJoinTicketConsumptionEvidence::Decode(Response->GetContentAsString(), Evidence)
		|| !Evidence.Consumption.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive)
		|| !Evidence.ServerId.Equals(Recovery->Scope.ServerId, ESearchCase::CaseSensitive)
		|| !Evidence.ConsumptionRequestId.Equals(Recovery->RequestId, ESearchCase::CaseSensitive)
		|| !Evidence.Consumption.SteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive)) return;
	Recovery->EvidenceCharacterId = Evidence.Consumption.CharacterId;
	Recovery->EvidenceGeneration = Evidence.Consumption.ConnectionGeneration;
	Recovery->LastEvidenceOutcome = FDungeonConsumeRecovery::EEvidenceOutcome::ConsumptionRecorded;
	QueueDungeonConsumeEvidence(Ticket, Recovery);
}

bool UHttpWebManager::QueueDungeonConsumeEvidence(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery || !Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || Recovery->EvidenceGeneration <= 0
		|| Recovery->LostAtUtc.GetTicks() <= 0 || bDungeonConsumeRecoveryCompleting) return false;
	TGuardValue<bool> HandoffGuard(bDungeonConsumeRecoveryCompleting, true);
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	if (!ReportConfiguredDungeonMemberDisconnection(Recovery->Scope.SessionId, Recovery->EvidenceCharacterId,
		Recovery->ExpectedSteamId, Recovery->EvidenceGeneration, Recovery->LostAtUtc)) return false;
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->Resolve(Recovery->RequestId)) return false;
	// Only durable ownership transfer removes THIS uncertainty. Other intents and
	// the outbox/persistence-failure barrier remain authoritative.
	DungeonConsumeRecoveries.Remove(Ticket);
	return true;
}

bool UHttpWebManager::ResolveCancelledDungeonConsumeRecovery(const FString& Ticket,
	const TSharedRef<FDungeonConsumeRecovery>& Recovery)
{
	if (DungeonConsumeRecoveries.FindRef(Ticket) != Recovery || !Recovery->bExpired
		|| PendingDungeonConsumeObservationWrites.Contains(Recovery->RequestId)
		|| !Recovery->bCancellationVerified || Recovery->EvidenceGeneration != 0
		|| !IsCurrentDungeonMutationScope(Recovery->Scope) || bDungeonConsumeRecoveryCompleting) return false;
	TGuardValue<bool> CompletionGuard(bDungeonConsumeRecoveryCompleting, true);
	Recovery->NextAttemptAt = FPlatformTime::Seconds() + 30.0;
	// The backend proof fences late consumes, but does not delete our durable
	// observation. Keep the intent if removal fails; no generation/disconnect is invented.
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->Resolve(Recovery->RequestId)) return false;
	DungeonConsumeRecoveries.Remove(Ticket);
	return true;
}

void UHttpWebManager::InitializeDungeonConsumeRecoveries(const FString& Directory)
{
	DungeonConsumeJournal = MakeShared<FRPGDungeonConsumeJournal>(Directory, ApiUrl, GameServerId, ConfiguredDungeonSessionId);
	bDungeonConsumeRestorePending = true;
	if (!RestoreDungeonConsumeRecoveries())
	{
		UE_LOG(LogTemp, Warning, TEXT("Dungeon pending-consumption loss journal could not be restored; files retained, retry scheduled and new mutations blocked."));
	}
	NextDungeonJournalRestoreAt = FPlatformTime::Seconds() + 1.0;
}

bool UHttpWebManager::RestoreDungeonConsumeRecoveries()
{
	if (!DungeonConsumeJournal || !DungeonConsumeJournal->RecoverForRetry()) return false;
	// Validate every collision before publishing anything. Preserve any live intent,
	// request/delegate ownership and its first loss time instead of overwriting it.
	for (const auto& Entry : DungeonConsumeJournal->GetPending())
		for (const auto& Existing : DungeonConsumeRecoveries)
		{
			const auto& State = Existing.Value;
			if (State->RequestId != Entry.Key) continue;
			const FRPGDungeonConsumeLossRecord Known {State->RequestId, State->TicketHash, State->ExpectedSteamId, State->LostAtUtc};
			if (!Known.SameAs(Entry.Value) || !DungeonConsumeJournal->MatchesAssignment(
				State->Scope.BackendUrl, State->Scope.ServerId, State->Scope.SessionId))
			{
				bDungeonConsumePersistenceFailed = true;
				return false;
			}
		}
	for (const auto& Entry : DungeonConsumeJournal->GetPending())
	{
		bool bAlreadyOwned = false;
		for (const auto& Existing : DungeonConsumeRecoveries)
			if (Existing.Value->RequestId == Entry.Key) { bAlreadyOwned = true; break; }
		if (bAlreadyOwned) continue;
		const auto Recovery = MakeShared<FDungeonConsumeRecovery>();
		Recovery->Scope = CaptureDungeonMutationScope();
		Recovery->RequestId = Entry.Value.RequestId;
		Recovery->TicketHash = Entry.Value.TicketHash;
		Recovery->ExpectedSteamId = Entry.Value.ExpectedSteamId;
		Recovery->LostAtUtc = Entry.Value.ObservedAtUtc;
		Recovery->bExpired = true;
		Recovery->bUncertain = true;
		Recovery->bRestoredWorldPending = true;
		// No original socket/delegate or ticket survives: reconcile proof only, never re-consume/admit.
		DungeonConsumeRecoveries.Add(TEXT("restored:") + Entry.Key, Recovery);
	}
	bDungeonConsumeRestorePending = false;
	return true;
}

void UHttpWebManager::BindRestoredDungeonConsumeWorld()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| !World->GetBegunPlay()
		|| World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_Client
		|| bDungeonDisconnectionReportShutdown) return;
	for (auto& Entry : DungeonConsumeRecoveries)
	{
		auto& State = Entry.Value;
		if (!State->bRestoredWorldPending || !State->Scope.BackendUrl.Equals(ApiUrl, ESearchCase::CaseSensitive)
			|| !State->Scope.ServerId.Equals(GameServerId, ESearchCase::CaseSensitive)
			|| !State->Scope.SessionId.Equals(ConfiguredDungeonSessionId, ESearchCase::CaseSensitive)) continue;
		// Initialization creates a Game-typed dummy world. Adopt the first begun-play world
		// ONCE, never migrate a callback/intent after it has been bound.
		State->Scope.World = World;
		State->bRestoredWorldPending = false;
	}
}

void UHttpWebManager::TickDungeonConsumeRecoveries()
{
	if (bDungeonConsumeRecoveryCompleting) return; // Report enqueue can synchronously tick this subsystem.
	BindRestoredDungeonConsumeWorld();
	RetryDungeonConsumeObservationWrites(FPlatformTime::Seconds());
	TArray<FString> Tickets;
	DungeonConsumeRecoveries.GenerateKeyArray(Tickets);
	int32 Started = 0;
	for (const FString& Ticket : Tickets)
	{
		FHttpRequestPtr Request = TakeDueDungeonConsumeRetry(Ticket, FPlatformTime::Seconds());
		if (Request.IsValid()) DispatchDungeonConsumeAttempt(Ticket, Request);
		else
		{
			Request = TakeDueDungeonConsumeEvidence(Ticket, FPlatformTime::Seconds());
			if (!Request.IsValid()) continue;
			DispatchDungeonConsumeEvidence(Ticket, Request);
		}
		if (++Started >= 4) break;
	}
}

bool UHttpWebManager::ConfirmConfiguredDungeonMemberAdmission(
	const FString& DungeonSessionId, const FString& CharacterId,
	const FString& SteamId, int64 ConnectionGeneration,
	TFunction<void(bool, int32)> Completion)
{
	FString Body;
	if (!IsHttpBackendServerRuntime() || !IsConfiguredForDungeonServer()
		|| DungeonSessionId != ConfiguredDungeonSessionId
		|| !FRPGDungeonConnectionRequest::Encode(DungeonSessionId, CharacterId,
			SteamId, ConnectionGeneration, Body)
		|| BackendServiceToken.IsEmpty()
		|| BackendServiceToken.Len() > 4096) return false;
	for (const TCHAR Character : BackendServiceToken)
		if (Character <= TEXT(' ') || Character >= TEXT('\x7f')) return false;

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response,
			bool bTransportSucceeded) mutable
		{
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			bool bConfirmed = false;
			if (bTransportSucceeded && Status == 200 && Response.IsValid()
				&& Response->GetContentLength() <= 4096)
				bConfirmed = FRPGDungeonAdmissionConfirmation::Decode(
					Response->GetContentAsString(), Status);
			if (Completion) Completion(bConfirmed, Status);
		});
	Request->SetURL(ApiUrl / FString::Printf(
		TEXT("dungeon-sessions/%s/members/%s/admission-confirmation"),
		*DungeonSessionId, *CharacterId));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	Request->SetContentAsString(Body);
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds)
		? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request->ProcessRequest();
}

void UHttpWebManager::InitializeDungeonDisconnectionReports()
{
	// Validate the session before using it as a path component.
	FGuid Session;
	if (!FGuid::Parse(ConfiguredDungeonSessionId, Session) || !Session.IsValid()
		|| !ConfiguredDungeonSessionId.Equals(Session.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)) return;
	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Online/DungeonDisconnections"),
		FMD5::HashAnsiString(*ApiUrl), FMD5::HashAnsiString(*GameServerId), ConfiguredDungeonSessionId);
	InitializeDungeonDisconnectionReports(Directory);
}

void UHttpWebManager::InitializeDungeonDisconnectionReports(const FString& Directory)
{
	// Initialize once per assigned manager, including when the first read fails.
	if (DungeonDisconnectionJournal || bDungeonDisconnectionReportShutdown) return;
	DungeonDisconnectionJournal = MakeShared<FRPGDungeonDisconnectionJournal>(
		Directory, ApiUrl, GameServerId, ConfiguredDungeonSessionId);
	bDungeonDisconnectionRestorePending = true;
	if (!RestoreDungeonDisconnectionReports())
	{
		UE_LOG(LogTemp, Warning, TEXT("Dungeon connection loss journal could not be restored; files retained and retry scheduled."));
	}
	InitializeDungeonConsumeRecoveries(FPaths::Combine(Directory, TEXT("ConsumeRecoveries")));
	DungeonJournalRestoreRetries = 0;
	// A failed parent restore must not prevent its own recovery ticker from starting.
	DungeonDisconnectionTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickDungeonDisconnectionReports), 1.f);
}

bool UHttpWebManager::RestoreDungeonDisconnectionReports()
{
	if (!DungeonDisconnectionJournal || !DungeonDisconnectionJournal->RecoverForRetry()) return false;
	for (const auto& Entry : DungeonDisconnectionJournal->GetPending())
		if (const auto Existing = DungeonDisconnectionReports.FindRef(Entry.Key))
			if (Existing->Body != Entry.Value.Body || Existing->SteamId != Entry.Value.SteamId)
			{
				bDungeonDisconnectionPersistenceFailed = true;
				return false;
			}
	for (const auto& Entry : DungeonDisconnectionJournal->GetPending())
	{
		if (DungeonDisconnectionReports.Contains(Entry.Key)) continue;
		auto Report = MakeShared<FDungeonDisconnectionReport>();
		Report->DungeonSessionId = ConfiguredDungeonSessionId;
		Report->CharacterId = Entry.Value.CharacterId;
		Report->SteamId = Entry.Value.SteamId;
		Report->ConnectionGeneration = Entry.Value.ConnectionGeneration;
		Report->ObservedAtUtc = Entry.Value.ObservedAtUtc;
		Report->Body = Entry.Value.Body;
		DungeonDisconnectionReports.Add(Entry.Key, MoveTemp(Report));
	}
	bDungeonDisconnectionRestorePending = false;
	if (!DungeonDisconnectionReports.IsEmpty())
		UE_LOG(LogTemp, Display, TEXT("Dungeon connection loss journal restored %d pending reports."),
			DungeonDisconnectionReports.Num());
	return true;
}

void UHttpWebManager::RetryDungeonJournalRestores(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting
		|| (!bDungeonDisconnectionRestorePending && !bDungeonConsumeRestorePending) || Now < NextDungeonJournalRestoreAt
		|| !DungeonDisconnectionJournal || !DungeonConsumeJournal
		|| !DungeonDisconnectionJournal->MatchesAssignment(ApiUrl, GameServerId, ConfiguredDungeonSessionId)
		|| !DungeonConsumeJournal->MatchesAssignment(ApiUrl, GameServerId, ConfiguredDungeonSessionId)) return;
	NextDungeonJournalRestoreAt = Now + (DungeonJournalRestoreRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> RestoreGuard(bDungeonConsumeRecoveryCompleting, true);
	// Loaded journals retain their in-flight objects and ACK/removal state. An
	// independent failed child must not rehydrate or reset a healthy parent.
	if (bDungeonDisconnectionRestorePending) RestoreDungeonDisconnectionReports();
	if (bDungeonConsumeRestorePending) RestoreDungeonConsumeRecoveries();
}

bool UHttpWebManager::TickDungeonDisconnectionReports(float)
{
	if (bDungeonDisconnectionReportShutdown) return false;
	// Local restoration needs no socket, credential or begun-play world. Restored
	// consume evidence still binds to the first real world before any HTTP work.
	RetryDungeonJournalRestores(FPlatformTime::Seconds());
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!IsValid(World) || World->bIsTearingDown || !World->IsGameWorld()
		|| World->GetGameInstance() != GetGameInstance()) return true;
	RetryDungeonDisconnectionWrites(FPlatformTime::Seconds());
	TArray<TSharedPtr<FDungeonDisconnectionReport>> Reports;
	DungeonDisconnectionReports.GenerateValueArray(Reports);
	const double Now = FPlatformTime::Seconds();
	int32 Started = 0;
	for (const auto& Report : Reports)
	{
		if (Report->Request.IsValid() || Now < Report->NextAttemptAt) continue;
		if (!SendDungeonDisconnectionAttempt(Report.ToSharedRef())) Report->NextAttemptAt = Now + 30.0;
		if (++Started >= 4) break;
	}
	TickDungeonConsumeRecoveries();
	return true;
}

bool UHttpWebManager::ReportConfiguredDungeonMemberDisconnection(
	const FString& DungeonSessionId, const FString& CharacterId,
	const FString& SteamId, int64 ConnectionGeneration, FDateTime ObservedAtUtc)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown) return false;
	FString Body;
	// Local custody must survive a temporarily unavailable service credential.
	// IsConfiguredForDungeonServer also requires that token; HTTP still uses it below.
	if (!IsHttpBackendServerRuntime() || GameServerId.IsEmpty() || ConfiguredDungeonSessionId.IsEmpty()
		|| !DungeonDisconnectionJournal || !DungeonDisconnectionJournal->MatchesAssignment(ApiUrl, GameServerId, DungeonSessionId)
		|| DungeonSessionId != ConfiguredDungeonSessionId
		|| !FRPGDungeonConnectionRequest::Encode(DungeonSessionId, CharacterId,
			SteamId, ConnectionGeneration, Body))
		return false;
	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = CharacterId;
	Record.SteamId = SteamId;
	Record.ConnectionGeneration = ConnectionGeneration;
	const FString Key = Record.Key();
	if (const auto* PendingWrite = PendingDungeonDisconnectionWrites.Find(Key))
	{
		if (!PendingWrite->Report || !PendingWrite->Report->SteamId.Equals(SteamId, ESearchCase::CaseSensitive)
			|| !IsCurrentDungeonMutationScope(PendingWrite->Scope)) return false;
		// A retry never replaces the original first loss, even before it reached disk.
		const bool bSaved = PersistDungeonDisconnectionWrite(Key, *PendingWrite);
		if (bSaved && !bDungeonConsumeRecoveryCompleting) TickDungeonDisconnectionReports(0.f);
		return bSaved;
	}
	if (const auto* Existing = DungeonDisconnectionJournal->GetPending().Find(Key))
	{
		if (!Existing->SteamId.Equals(SteamId, ESearchCase::CaseSensitive)) return false;
		// A duplicate cleanup must not replace the original timestamp, including v1.
		if (DungeonDisconnectionJournal->IsHealthy() && DungeonDisconnectionReports.Contains(Key)) return true;
		Record = *Existing;
	}
	else
	{
		if (ObservedAtUtc.GetTicks() <= 0) return false;
		Record.ObservedAtUtc = ObservedAtUtc;
		if (!Record.EncodeBody(DungeonSessionId, Record.Body)) return false;
	}
	const TSharedRef<FDungeonDisconnectionReport> Report =
		MakeShared<FDungeonDisconnectionReport>();
	Report->DungeonSessionId = DungeonSessionId;
	Report->CharacterId = CharacterId;
	Report->SteamId = SteamId;
	Report->ConnectionGeneration = ConnectionGeneration;
	Report->ObservedAtUtc = Record.ObservedAtUtc;
	Report->Body = Record.Body;
	if (const auto Existing = DungeonDisconnectionReports.FindRef(Key))
	{
		if (Existing->Body != Report->Body || Existing->SteamId != Report->SteamId)
		{
			bDungeonDisconnectionPersistenceFailed = true;
			return false;
		}
	}
	if (PendingDungeonDisconnectionWrites.IsEmpty())
	{
		DungeonDisconnectionWriteRetries = 0;
		NextDungeonDisconnectionWriteAt = FPlatformTime::Seconds() + 1.0;
	}
	FDungeonDisconnectionWrite Write {CaptureDungeonMutationScope(), Report};
	PendingDungeonDisconnectionWrites.Add(Key, Write);
	if (!PersistDungeonDisconnectionWrite(Key, Write)) return false;
	// The ticker also resumes this entry after a world change or process restart.
	if (!bDungeonConsumeRecoveryCompleting) TickDungeonDisconnectionReports(0.f);
	return true;
}

bool UHttpWebManager::PersistDungeonDisconnectionWrite(const FString& Key, const FDungeonDisconnectionWrite& Write)
{
	if (bDungeonDisconnectionRestorePending || !Write.Report || !IsCurrentDungeonMutationScope(Write.Scope) || !DungeonDisconnectionJournal
		|| !DungeonDisconnectionJournal->MatchesAssignment(Write.Scope.BackendUrl, Write.Scope.ServerId, Write.Scope.SessionId)) return false;
	const auto Report = Write.Report; // Own it before removing the map entry below.
	const FRPGDungeonDisconnectionRecord Record {Report->CharacterId, Report->SteamId,
		Report->ConnectionGeneration, Report->ObservedAtUtc, Report->Body};
	if (!DungeonDisconnectionJournal->Persist(Record)) return false;
	if (const auto Existing = DungeonDisconnectionReports.FindRef(Key))
	{
		if (Existing->Body != Report->Body || Existing->SteamId != Report->SteamId)
		{
			bDungeonDisconnectionPersistenceFailed = true;
			return false;
		}
	}
	else DungeonDisconnectionReports.Add(Key, Report);
	PendingDungeonDisconnectionWrites.Remove(Key);
	return true;
}

void UHttpWebManager::RetryDungeonDisconnectionWrites(double Now)
{
	if (!IsInGameThread() || bDungeonDisconnectionReportShutdown || bDungeonConsumeRecoveryCompleting || bDungeonDisconnectionRestorePending
		|| PendingDungeonDisconnectionWrites.IsEmpty() || Now < NextDungeonDisconnectionWriteAt) return;
	NextDungeonDisconnectionWriteAt = Now + (DungeonDisconnectionWriteRetries++ == 0 ? 3.0 : 30.0);
	TGuardValue<bool> WriteGuard(bDungeonConsumeRecoveryCompleting, true);
	if (!DungeonDisconnectionJournal) return;
	TArray<FString> Keys; PendingDungeonDisconnectionWrites.GenerateKeyArray(Keys);
	const int32 Offset = static_cast<int32>((static_cast<int64>(DungeonDisconnectionWriteRetries - 1) * 4) % Keys.Num());
	int32 Attempts = 0;
	bool bStorageChecked = false;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		const FString Key = Keys[(Offset + Index) % Keys.Num()];
		const auto* Write = PendingDungeonDisconnectionWrites.Find(Key);
		if (!Write || !IsCurrentDungeonMutationScope(Write->Scope)
			|| !DungeonDisconnectionJournal->MatchesAssignment(Write->Scope.BackendUrl, Write->Scope.ServerId, Write->Scope.SessionId)) continue;
		if (!bStorageChecked)
		{
			if (!DungeonDisconnectionJournal->IsHealthy() && !DungeonDisconnectionJournal->RecoverForRetry()) return;
			bStorageChecked = true;
		}
		PersistDungeonDisconnectionWrite(Key, *Write);
		if (++Attempts >= 4) break;
	}
}

bool UHttpWebManager::ScheduleDungeonDisconnectionRetry(
	const TSharedRef<FDungeonDisconnectionReport>& Report,
	int32 CompletedAttempt, int32 HttpStatus)
{
	if (bDungeonDisconnectionReportShutdown || Report->Attempts != CompletedAttempt) return false;
	float Delay = FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(
		CompletedAttempt, HttpStatus);
	if (Delay < 0.f) Delay = 30.f;
	Report->NextAttemptAt = FPlatformTime::Seconds() + Delay;
	return true;
}

bool UHttpWebManager::SendDungeonDisconnectionAttempt(
	const TSharedRef<FDungeonDisconnectionReport>& Report)
{
	FHttpRequestPtr Request = CreateDungeonDisconnectionAttempt(Report);
	if (!Request.IsValid()) return false;
	if (Request->ProcessRequest()) return true;
	Request->OnProcessRequestComplete().Unbind();
	Report->Request.Reset();
	return ScheduleDungeonDisconnectionRetry(Report, Report->Attempts, 0);
}

FHttpRequestPtr UHttpWebManager::CreateDungeonDisconnectionAttempt(
	const TSharedRef<FDungeonDisconnectionReport>& Report)
{
	FString Body;
	FRPGDungeonDisconnectionRecord Record;
	Record.CharacterId = Report->CharacterId;
	Record.SteamId = Report->SteamId;
	Record.ConnectionGeneration = Report->ConnectionGeneration;
	Record.ObservedAtUtc = Report->ObservedAtUtc;
	const FString Key = Record.Key();
	const auto* Durable = DungeonDisconnectionJournal ? DungeonDisconnectionJournal->GetPending().Find(Key) : nullptr;
	if (bDungeonDisconnectionReportShutdown || bDungeonDisconnectionRestorePending || !IsHttpBackendServerRuntime()
		|| !IsConfiguredForDungeonServer()
		|| !DungeonDisconnectionJournal || !DungeonDisconnectionJournal->IsHealthy()
		|| !DungeonDisconnectionJournal->MatchesAssignment(ApiUrl, GameServerId, Report->DungeonSessionId)
		|| PendingDungeonDisconnectionWrites.Contains(Key) || !Durable || Durable->Body != Report->Body || Durable->SteamId != Report->SteamId
		|| Report->Request.IsValid()
		|| Report->DungeonSessionId != ConfiguredDungeonSessionId
		|| !Record.EncodeBody(Report->DungeonSessionId, Body)
		|| !Body.Equals(Report->Body, ESearchCase::CaseSensitive)
		|| BackendServiceToken.IsEmpty() || BackendServiceToken.Len() > 4096)
		return nullptr;
	for (const TCHAR Character : BackendServiceToken)
		if (Character <= TEXT(' ') || Character >= TEXT('\x7f')) return nullptr;
	Report->Attempts = FMath::Min(Report->Attempts + 1, 3);
	const int32 Attempt = Report->Attempts;
	const FDungeonMutationScope Scope = CaptureDungeonMutationScope();

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Report->Request = Request;
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Report, Attempt, Key, Scope](FHttpRequestPtr CompletedRequest, FHttpResponsePtr Response,
			bool bTransportSucceeded)
		{
			const auto* Current = DungeonDisconnectionReports.Find(Key);
			if (bDungeonDisconnectionReportShutdown || !Current || Current->Get() != &Report.Get()
				|| CompletedRequest != Report->Request || !DungeonDisconnectionJournal) return;
			Report->Request.Reset();
			if (!IsCurrentDungeonMutationScope(Scope) || !DungeonDisconnectionJournal->MatchesAssignment(Scope.BackendUrl, Scope.ServerId, Scope.SessionId)) return;
			const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
			const FString ResponseBody = Response.IsValid() && Response->GetContentLength() <= 4096
				? Response->GetContentAsString() : FString();
			const bool bAccepted = bTransportSucceeded
				&& FRPGDungeonDisconnectionJournal::IsAcceptedResponse(ResponseBody, Status);
			if (bTransportSucceeded && (bAccepted || FRPGDungeonDisconnectionJournal::IsTerminalRejection(ResponseBody, Status))
				&& DungeonDisconnectionJournal->Resolve(Key))
			{
				DungeonDisconnectionReports.Remove(Key);
				return;
			}
			ScheduleDungeonDisconnectionRetry(Report, Attempt, bTransportSucceeded ? Status : 0);
		});
	Request->SetURL(ApiUrl / FString::Printf(
		TEXT("dungeon-sessions/%s/members/%s/disconnection"),
		*Report->DungeonSessionId, *Report->CharacterId));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	Request->SetContentAsString(Report->Body);
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds)
		? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request;
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

void UHttpWebManager::OnConsumeJoinTicketResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString JoinTicket)
{
	const auto Recovery = DungeonConsumeRecoveries.FindRef(JoinTicket);
	if (!Recovery) return;
	if (!IsCurrentDungeonMutationScope(Recovery->Scope))
	{
		DungeonConsumeRecoveries.Remove(JoinTicket);
		return;
	}
	if (FPlatformTime::Seconds() >= Recovery->Deadline)
	{
		ExpireDungeonConsumeRecovery(JoinTicket, Recovery.ToSharedRef());
		return;
	}
	const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	FRPGJoinTicketAdmission Admission;
	bool bDecoded = false;

	if (bSuccess)
	{
		bDecoded = Response->GetContentLength() <= 8192
			&& FRPGJoinTicketAdmission::Decode(Response->GetContentAsString(), Admission);
		bSuccess = bDecoded && Admission.DungeonSessionId.Equals(Recovery->Scope.SessionId, ESearchCase::CaseSensitive);
	}
	const bool bUncertain = !bWasSuccessful || !Response.IsValid() || Status <= 0
		|| (Status == 401 && Recovery->bUncertain && !IsDefinitiveDungeonConsumeDenial(Response))
		|| Status == 408 || Status == 425 || Status == 429 || Status >= 500
		|| (EHttpResponseCodes::IsOk(Status) && !bSuccess);
	if (!bSuccess && bUncertain)
	{
		Recovery->bUncertain = true;
		Recovery->bWaiting = true;
		const double Delay = Recovery->Retries == 0 ? 1.0 : Recovery->Retries == 1 ? 3.0 : 30.0;
		++Recovery->Retries;
		Recovery->NextAttemptAt = FMath::Min(FPlatformTime::Seconds() + Delay, Recovery->Deadline);
		return; // Keep GameMode's exact original connection and first-loss observation alive.
	}
	if (Recovery->LostAtUtc.GetTicks() > 0)
	{
		// This peer is already gone. Even a successful late consume reply is historical
		// evidence, not permission to discard the durable loss or welcome a new socket.
		ExpireDungeonConsumeRecovery(JoinTicket, Recovery.ToSharedRef());
		if (bSuccess && Admission.SteamId.Equals(Recovery->ExpectedSteamId, ESearchCase::CaseSensitive))
		{
			Recovery->EvidenceCharacterId = Admission.CharacterId;
			Recovery->EvidenceGeneration = Admission.ConnectionGeneration;
			QueueDungeonConsumeEvidence(JoinTicket, Recovery.ToSharedRef());
		}
		return;
	}
	// Hold the uncertainty barrier until synchronous GameMode handoff has had a
	// chance to durably queue an already observed socket loss.
	TGuardValue<bool> CompletionGuard(bDungeonConsumeRecoveryCompleting,
		bDungeonConsumeRecoveryCompleting || Recovery->bUncertain);
	DungeonConsumeRecoveries.Remove(JoinTicket);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Join ticket validation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnJoinTicketConsumed.Broadcast(
		JoinTicket,
		Admission.DungeonSessionId,
		Admission.CharacterId,
		Admission.SteamId,
		Admission.ConnectionGeneration,
		bSuccess);
}
