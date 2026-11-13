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
}

UObject& UHttpWebManager::GetSessionTransportOwner() const { return *const_cast<UHttpWebManager*>(this); }
FRPGDungeonSessionTransportConfig UHttpWebManager::GetSessionTransportConfig() const
{
	return { ApiUrl, GameServerId, ConfiguredDungeonSessionId, RequestTimeoutSeconds,
		IsHttpBackendServerRuntime(), IsConfiguredForDungeonServer(), PlayerDungeonRequestGeneration };
}
bool UHttpWebManager::HasSessionAccessToken() const { return HasAccessToken(); }
void UHttpWebManager::ApplySessionTransportHeaders(const FHttpRequestRef& Request) const { ApplyCommonHeaders(Request); }
bool UHttpWebManager::AcceptSessionPlayerResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response, bool& bWasSuccessful)
{ return AcceptPlayerResponse(Request, Response, bWasSuccessful); }
void UHttpWebManager::DispatchSessionTransportRequest(const FHttpRequestRef& Request) { Request->ProcessRequest(); }
void UHttpWebManager::BroadcastSessionTransportResult(const FRPGDungeonSession& Session, bool bSuccess)
{ OnDungeonSessionUpdated.Broadcast(Session, bSuccess); }

void UHttpWebManager::CreateDungeonSession(const FString& CharacterId, const FString& DungeonId, const FString& Difficulty)
{ DungeonSessionTransport->Create(CharacterId, DungeonId, Difficulty); }
void UHttpWebManager::JoinDungeonSession(const FString& SessionId, const FString& CharacterId)
{ DungeonSessionTransport->Join(SessionId, CharacterId); }
void UHttpWebManager::LoadDungeonSession(const FString& SessionId) { DungeonSessionTransport->Load(SessionId); }
void UHttpWebManager::LoadActiveDungeonSession(const FString& CharacterId) { DungeonSessionTransport->LoadActive(CharacterId); }
void UHttpWebManager::LeaveDungeonSession(const FString& SessionId, const FString& CharacterId)
{ DungeonSessionTransport->Leave(SessionId, CharacterId); }
void UHttpWebManager::SetDungeonDeparture(const FString& SessionId, const FString& CharacterId,
	const FString& RequestId, int64 ExpectedRevision, bool bCancel)
{ DungeonSessionTransport->SetDeparture(SessionId, CharacterId, RequestId, ExpectedRevision, bCancel); }
void UHttpWebManager::SendConfiguredDungeonSessionRequest(const FString& Action, const FString& Outcome)
{ DungeonSessionTransport->SendConfigured(Action, Outcome); }
void UHttpWebManager::OnDungeonSessionResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, FString Operation, FString ExpectedSessionId, uint64 RequestGeneration)
{ DungeonSessionTransport->HandleResponse(Request, Response, bWasSuccessful, Operation, ExpectedSessionId, RequestGeneration); }

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
