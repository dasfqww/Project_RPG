#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Online/RPGDungeonAbandonment.h"

class UObject;

/** Public identity and callback scope only; never contains a token. */
struct FRPGDungeonPlayerTransportConfig
{
	FString ApiUrl;
	FString SteamId;
	float TimeoutSeconds = 10.f;
	uint64 PlayerGeneration = 0;
	bool bServerRuntime = false;
};

class IRPGDungeonPlayerTransportHost
{
public:
	virtual ~IRPGDungeonPlayerTransportHost() = default;
	virtual UObject& GetDungeonPlayerTransportOwner() const = 0;
	virtual FRPGDungeonPlayerTransportConfig GetDungeonPlayerTransportConfig() const = 0;
	virtual bool HasDungeonPlayerToken() const = 0;
	virtual void ApplyJoinTicketHeaders(const FHttpRequestRef& Request) const = 0;
	virtual void ApplyAbandonmentAuthorization(const FHttpRequestRef& Request) const = 0;
	virtual bool AcceptDungeonPlayerResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response,
		bool& bWasSuccessful) = 0;
	virtual bool DispatchDungeonPlayerRequest(const FHttpRequestRef& Request) = 0;
	virtual void BroadcastPlayerJoinTicketResult(const FString& SessionId, const FString& CharacterId,
		const FString& Ticket, const FString& ExpiresAt, bool bSuccess, const FString& ErrorCode = FString()) = 0;
	virtual void BroadcastPlayerAbandonmentResult(const FRPGDungeonAbandonmentReceipt& Receipt,
		bool bSuccess, const FString& ErrorCode) = 0;
};

/** Player ticket issuance and explicit abandonment HTTP only. The host retains credential
 * policy, shared client generation and gameplay/UI state. No automatic retry, request
 * serialization, local departure or server ticket consumption is introduced here.
 */
class PROJECT_RPG_API FRPGDungeonPlayerTransport : public TSharedFromThis<FRPGDungeonPlayerTransport>
{
public:
	explicit FRPGDungeonPlayerTransport(IRPGDungeonPlayerTransportHost& InHost) : Host(InHost) {}
	FRPGDungeonPlayerTransport(const FRPGDungeonPlayerTransport&) = delete;
	FRPGDungeonPlayerTransport& operator=(const FRPGDungeonPlayerTransport&) = delete;
	void RequestJoinTicket(const FString& CharacterId, const FString& SessionId);
	void HandleJoinTicketResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
		FString CharacterId, FString SessionId, uint64 RequestGeneration);
	void Abandon(const FString& SessionId, const FString& CharacterId, const FString& ExpectedSteamId);
	/** Build only; the existing manager test seam preserves its original validation boundary. */
	FHttpRequestRef CreateAbandonRequest(const FString& SessionId, const FString& CharacterId, const FString& ExpectedSteamId);
	void HandleAbandonResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
		FString SessionId, FString CharacterId, FString ExpectedSteamId, uint64 RequestGeneration);
private:
	IRPGDungeonPlayerTransportHost& Host;
};
