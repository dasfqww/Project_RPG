#include "Manager/HttpWebManager.h"

#include "HttpWebManagerRuntime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformTime.h"

namespace
{
	using RPGHttpWebManager::IsHttpBackendServerRuntime;

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
