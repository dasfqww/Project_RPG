#include "Online/RPGDungeonPlayerTransport.h"

#include "Online/RPGBackendPlayerAuthState.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

void FRPGDungeonPlayerTransport::RequestJoinTicket(
	const FString& CharacterId,
	const FString& DungeonSessionId)
{
	if (CharacterId.IsEmpty()
		|| DungeonSessionId.IsEmpty()
		|| !Host.HasDungeonPlayerToken())
	{
		Host.BroadcastPlayerJoinTicketResult(
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
		Host.BroadcastPlayerJoinTicketResult(
			DungeonSessionId,
			CharacterId,
			FString(),
			FString(),
			false);
		return;
	}

	const auto Config = Host.GetDungeonPlayerTransportConfig();
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	const TWeakPtr<FRPGDungeonPlayerTransport> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetDungeonPlayerTransportOwner(),
		[WeakSelf, CharacterId, DungeonSessionId, Generation = Config.PlayerGeneration]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const auto Self = WeakSelf.Pin())
				Self->HandleJoinTicketResponse(Completed, Response, bSuccess, CharacterId, DungeonSessionId, Generation);
		});
	Request->SetURL(Config.ApiUrl / TEXT("join-tickets"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyJoinTicketHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(Config.TimeoutSeconds);
	Host.DispatchDungeonPlayerRequest(Request);
}

void FRPGDungeonPlayerTransport::HandleJoinTicketResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString CharacterId,
	FString DungeonSessionId,
	uint64 RequestGeneration)
{
	if (RequestGeneration != Host.GetDungeonPlayerTransportConfig().PlayerGeneration) return;
	if (!Host.AcceptDungeonPlayerResponse(Request, Response, bWasSuccessful)) return;
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

	Host.BroadcastPlayerJoinTicketResult(
		DungeonSessionId,
		CharacterId,
		JoinTicket,
		ExpiresAt,
		bSuccess, ErrorCode);
}

void FRPGDungeonPlayerTransport::Abandon(const FString& DungeonSessionId,
	const FString& CharacterId, const FString& ExpectedSteamId)
{
	const auto Config = Host.GetDungeonPlayerTransportConfig();
	FRPGDungeonAbandonmentReceipt Identity;
	Identity.DungeonSessionId = DungeonSessionId;
	Identity.CharacterId = CharacterId;
	FGuid SessionGuid, CharacterGuid;
	if (Config.bServerRuntime || !Host.HasDungeonPlayerToken()
		|| !FRPGBackendPlayerAuthState::IsSteamId(ExpectedSteamId)
		|| ExpectedSteamId != Config.SteamId
		|| !FGuid::ParseExact(DungeonSessionId, EGuidFormats::DigitsWithHyphens, SessionGuid) || !SessionGuid.IsValid()
		|| !FGuid::ParseExact(CharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid())
	{
		Host.BroadcastPlayerAbandonmentResult(Identity, false, TEXT("dungeon_abandonment_authentication_required"));
		return;
	}
	FHttpRequestRef Request = CreateAbandonRequest(DungeonSessionId, CharacterId, ExpectedSteamId);
	if (!Host.DispatchDungeonPlayerRequest(Request))
		HandleAbandonResponse(Request, nullptr, false, DungeonSessionId, CharacterId,
			ExpectedSteamId, Host.GetDungeonPlayerTransportConfig().PlayerGeneration);
}

FHttpRequestRef FRPGDungeonPlayerTransport::CreateAbandonRequest(const FString& SessionId,
	const FString& CharacterId, const FString& ExpectedSteamId)
{
	const auto Config = Host.GetDungeonPlayerTransportConfig();
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	const TWeakPtr<FRPGDungeonPlayerTransport> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetDungeonPlayerTransportOwner(),
		[WeakSelf, SessionId, CharacterId, ExpectedSteamId, Generation = Config.PlayerGeneration]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const auto Self = WeakSelf.Pin())
				Self->HandleAbandonResponse(Completed, Response, bSuccess, SessionId, CharacterId, ExpectedSteamId, Generation);
		});
	Request->SetURL(Config.ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/members/%s/abandon"),
		*FGenericPlatformHttp::UrlEncode(SessionId), *FGenericPlatformHttp::UrlEncode(CharacterId)));
	Request->SetVerb(TEXT("POST"));
	// Never fall back to a server/admin credential for this player mutation.
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Host.ApplyAbandonmentAuthorization(Request);
	Request->SetTimeout(Config.TimeoutSeconds);
	return Request;
}

void FRPGDungeonPlayerTransport::HandleAbandonResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, FString SessionId, FString CharacterId, FString ExpectedSteamId, uint64 RequestGeneration)
{
	const auto Config = Host.GetDungeonPlayerTransportConfig();
	// Discard stale callbacks BEFORE handling a 401; an old response cannot erase a renewed token.
	if (Config.bServerRuntime || RequestGeneration != Config.PlayerGeneration) return;
	FRPGDungeonAbandonmentReceipt Receipt;
	Receipt.DungeonSessionId = SessionId;
	Receipt.CharacterId = CharacterId;
	const bool bAccountMatches = ExpectedSteamId == Config.SteamId && !ExpectedSteamId.IsEmpty();
	const bool bAccepted = bAccountMatches && Host.AcceptDungeonPlayerResponse(Request, Response, bWasSuccessful);
	if (!bAccepted)
	{
		Host.BroadcastPlayerAbandonmentResult(Receipt, false, TEXT("dungeon_abandonment_authentication_changed"));
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
	Host.BroadcastPlayerAbandonmentResult(Receipt, bSuccess, bSuccess ? FString() : Error);
}
