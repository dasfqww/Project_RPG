#include "Manager/HttpWebManager.h"

#include "HttpWebManagerRuntime.h"
#include "Economy/RPGDungeonRewardSnapshot.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
	using RPGHttpWebManager::IsHttpBackendServerRuntime;
}

UObject& UHttpWebManager::GetRewardSettlementOwner() const { return *const_cast<UHttpWebManager*>(this); }
FRPGDungeonRewardSettlementConfig UHttpWebManager::GetRewardSettlementConfig() const
{
	return { ApiUrl, GameServerId, ConfiguredDungeonSessionId, RequestTimeoutSeconds, IsConfiguredForDungeonServer() };
}
bool UHttpWebManager::CanDispatchRewardSettlement() const { return CanDispatchConfiguredDungeonConnectionMutation(); }
TFunction<bool()> UHttpWebManager::CaptureRewardSettlementScope() const
{
	const FDungeonMutationScope Scope = CaptureDungeonMutationScope();
	const TWeakObjectPtr<const UHttpWebManager> WeakThis(this);
	return [WeakThis, Scope]()
	{
		return WeakThis.IsValid() && WeakThis->IsCurrentDungeonMutationScope(Scope);
	};
}
void UHttpWebManager::ApplyRewardSettlementHeaders(const FHttpRequestRef& Request) const { ApplyCommonHeaders(Request); }
void UHttpWebManager::BroadcastRewardSettlement(const FString& State, int32 HttpStatus, bool bSuccess)
{
	OnDungeonRewardSettlementRequested.Broadcast(State, HttpStatus, bSuccess);
}

void UHttpWebManager::SettleConfiguredDungeonRewards(const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges, const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	DungeonRewardSettlement->Settle(RewardVersion, CurrencyChanges, ItemRewards);
}
FHttpRequestPtr UHttpWebManager::CreateDungeonRewardSettlementRequest(const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges, const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	return DungeonRewardSettlement->CreateRequest(RewardVersion, CurrencyChanges, ItemRewards);
}
void UHttpWebManager::OnSettleDungeonRewardsResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, FString ExpectedRewardVersion)
{
	DungeonRewardSettlement->HandleResponse(Request, Response, bWasSuccessful, ExpectedRewardVersion);
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
