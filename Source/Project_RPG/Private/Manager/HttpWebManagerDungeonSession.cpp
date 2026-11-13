#include "Manager/HttpWebManager.h"

#include "HttpWebManagerRuntime.h"
#include "Interfaces/IHttpRequest.h"

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

void UHttpWebManager::AbandonDungeonSession(const FString& SessionId, const FString& CharacterId, const FString& ExpectedSteamId)
{ DungeonPlayerTransport->Abandon(SessionId, CharacterId, ExpectedSteamId); }
FHttpRequestRef UHttpWebManager::CreateDungeonAbandonmentRequest(const FString& SessionId, const FString& CharacterId, const FString& ExpectedSteamId)
{ return DungeonPlayerTransport->CreateAbandonRequest(SessionId, CharacterId, ExpectedSteamId); }
void UHttpWebManager::OnAbandonDungeonSessionResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, FString SessionId, FString CharacterId, FString ExpectedSteamId, uint64 RequestGeneration)
{ DungeonPlayerTransport->HandleAbandonResponse(Request, Response, bWasSuccessful, SessionId, CharacterId, ExpectedSteamId, RequestGeneration); }

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
