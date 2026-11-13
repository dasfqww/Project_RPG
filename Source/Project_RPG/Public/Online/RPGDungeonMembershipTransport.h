#pragma once

#include "CoreMinimal.h"
#include "Http.h"
#include "Online/RPGDungeonMembershipSnapshot.h"

class UObject;

/** Assignment only; the server credential stays with the host. */
struct FRPGDungeonMembershipTransportConfig
{
	FString ApiUrl;
	FString SessionId;
	FString ServerId;
	float TimeoutSeconds = 10.f;
	bool bServerConfigured = false;
	bool bServerRuntime = false;
};

class IRPGDungeonMembershipTransportHost
{
public:
	virtual ~IRPGDungeonMembershipTransportHost() = default;
	virtual UObject& GetMembershipTransportOwner() const = 0;
	virtual FRPGDungeonMembershipTransportConfig GetMembershipTransportConfig() const = 0;
	virtual bool HasValidMembershipCredential() const = 0;
	virtual void ApplyMembershipAuthorization(const FHttpRequestRef& Request) const = 0;
};

/** One shared request slot for membership reads and expiry processing. The host outlives
 * this service. Receipt interpretation remains in the existing codec/authority monitor.
 */
class PROJECT_RPG_API FRPGDungeonMembershipTransport : public TSharedFromThis<FRPGDungeonMembershipTransport>
{
public:
	explicit FRPGDungeonMembershipTransport(IRPGDungeonMembershipTransportHost& InHost) : Host(InHost) {}
	~FRPGDungeonMembershipTransport();
	FRPGDungeonMembershipTransport(const FRPGDungeonMembershipTransport&) = delete;
	FRPGDungeonMembershipTransport& operator=(const FRPGDungeonMembershipTransport&) = delete;
	void Initialize() { bShutdown = false; }
	/** Suppress callbacks before the host shuts down its other services; cancellation follows separately. */
	void BeginShutdown() { bShutdown = true; }
	void Cancel();
	bool IsActive() const { return ActiveRequest.IsValid(); }
	FHttpRequestPtr CreateSnapshotRequest(TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion);
	FHttpRequestPtr CreateExpirationRequest(TFunction<void()> Completion);
	bool LoadSnapshot(TFunction<void(FRPGDungeonMembershipSnapshotResult)> Completion);
	bool ProcessExpirations(TFunction<void()> Completion);
private:
	IRPGDungeonMembershipTransportHost& Host;
	FHttpRequestPtr ActiveRequest;
	uint64 RequestGeneration = 0;
	bool bShutdown = false;
	FHttpRequestPtr CreateBoundRequest(const FRPGDungeonMembershipTransportConfig& Config,
		const TCHAR* Action, const TCHAR* Verb, TFunction<void(FHttpResponsePtr, bool, bool)> Completion);
	bool Dispatch(const FHttpRequestPtr& Request);
};
