#pragma once

#include "CoreMinimal.h"
#include "Http.h"

struct FRPGCurrencyChange;
struct FRPGDungeonItemReward;
class UObject;

/** Assignment snapshot only. Credentials stay with the host's header policy. */
struct FRPGDungeonRewardSettlementConfig
{
	FString ApiUrl;
	FString ServerId;
	FString SessionId;
	float TimeoutSeconds = 10.f;
	bool bServerConfigured = false;
};

/** Current assignment, connection-loss barrier and notifications; no manager internals. */
class IRPGDungeonRewardSettlementHost
{
public:
	virtual ~IRPGDungeonRewardSettlementHost() = default;
	virtual UObject& GetRewardSettlementOwner() const = 0;
	virtual FRPGDungeonRewardSettlementConfig GetRewardSettlementConfig() const = 0;
	virtual bool CanDispatchRewardSettlement() const = 0;
	virtual TFunction<bool()> CaptureRewardSettlementScope() const = 0;
	virtual void ApplyRewardSettlementHeaders(const FHttpRequestRef& Request) const = 0;
	virtual void BroadcastRewardSettlement(const FString& State, int32 HttpStatus, bool bSuccess) = 0;
};

/** Owns settlement serialization, single-flight request correlation and cancellation.
 * The host outlives this service. Callbacks require both a live host UObject and service.
 * Snapshot reads and reward delivery remain separate from settlement acknowledgement.
 */
class PROJECT_RPG_API FRPGDungeonRewardSettlement
	: public TSharedFromThis<FRPGDungeonRewardSettlement>
{
	friend struct FRPGDungeonDisconnectionOrderingTestAccess;
public:
	explicit FRPGDungeonRewardSettlement(IRPGDungeonRewardSettlementHost& InHost) : Host(InHost) {}
	~FRPGDungeonRewardSettlement();
	FRPGDungeonRewardSettlement(const FRPGDungeonRewardSettlement&) = delete;
	FRPGDungeonRewardSettlement& operator=(const FRPGDungeonRewardSettlement&) = delete;
	bool IsActive() const { return ActiveRequest.IsValid(); }
	void Cancel();
	void Settle(const FString& RewardVersion, const TArray<FRPGCurrencyChange>& CurrencyChanges,
		const TArray<FRPGDungeonItemReward>& ItemRewards);
	/** Build only; the caller may dispatch after durable preparation or inject a completion in tests. */
	FHttpRequestPtr CreateRequest(const FString& RewardVersion, const TArray<FRPGCurrencyChange>& CurrencyChanges,
		const TArray<FRPGDungeonItemReward>& ItemRewards);
	/** Compatibility entrypoint for the existing manager callback. */
	void HandleResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful,
		const FString& ExpectedRewardVersion);
private:
	IRPGDungeonRewardSettlementHost& Host;
	FHttpRequestPtr ActiveRequest;
	bool ClaimCompletion(const FHttpRequestPtr& Completed);
};
