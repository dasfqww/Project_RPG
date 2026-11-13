#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Online/RPGDungeonSessionTypes.h"

class FJsonObject;
class UObject;

/** Current assignment and client callback generation; contains no credential. */
struct FRPGDungeonSessionTransportConfig
{
	FString ApiUrl;
	FString ServerId;
	FString SessionId;
	float TimeoutSeconds = 10.f;
	bool bServerRuntime = false;
	bool bServerConfigured = false;
	uint64 PlayerGeneration = 0;
};

class IRPGDungeonSessionTransportHost
{
public:
	virtual ~IRPGDungeonSessionTransportHost() = default;
	virtual UObject& GetSessionTransportOwner() const = 0;
	virtual FRPGDungeonSessionTransportConfig GetSessionTransportConfig() const = 0;
	virtual bool HasSessionAccessToken() const = 0;
	virtual void ApplySessionTransportHeaders(const FHttpRequestRef& Request) const = 0;
	virtual bool AcceptSessionPlayerResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response,
		bool& bWasSuccessful) = 0;
	virtual void DispatchSessionTransportRequest(const FHttpRequestRef& Request) = 0;
	virtual void BroadcastSessionTransportResult(const FRPGDungeonSession& Session, bool bSuccess) = 0;
};

/** Session request construction and response interpretation. The host retains authentication,
 * the common dungeon callback generation and gameplay flow. Requests remain concurrent;
 * weak host/service callbacks neither cancel nor roll back backend mutations.
 */
class PROJECT_RPG_API FRPGDungeonSessionTransport : public TSharedFromThis<FRPGDungeonSessionTransport>
{
public:
	explicit FRPGDungeonSessionTransport(IRPGDungeonSessionTransportHost& InHost) : Host(InHost) {}
	FRPGDungeonSessionTransport(const FRPGDungeonSessionTransport&) = delete;
	FRPGDungeonSessionTransport& operator=(const FRPGDungeonSessionTransport&) = delete;
	void Create(const FString& CharacterId, const FString& DungeonId, const FString& Difficulty);
	void Join(const FString& SessionId, const FString& CharacterId);
	void Load(const FString& SessionId);
	void LoadActive(const FString& CharacterId);
	void Leave(const FString& SessionId, const FString& CharacterId);
	void SetDeparture(const FString& SessionId, const FString& CharacterId, const FString& RequestId,
		int64 ExpectedRevision, bool bCancel);
	void SendConfigured(const FString& Action, const FString& Outcome = FString());
	void HandleResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
		const FString& Operation, const FString& ExpectedSessionId, uint64 RequestGeneration);
private:
	IRPGDungeonSessionTransportHost& Host;
	void SendRequest(const FString& RelativePath, const FString& Verb, const TSharedPtr<FJsonObject>& RequestObject,
		const FString& Operation, const FString& ExpectedSessionId);
};
