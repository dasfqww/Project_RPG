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
#include "Economy/RPGDungeonRewardSnapshot.h"

namespace
{
	using RPGHttpWebManager::IsHttpBackendServerRuntime;

	bool ClaimDungeonSettlementCompletion(FHttpRequestPtr& Active, const FHttpRequestPtr& Completed)
	{
		if (!Active.IsValid() || Active != Completed) return false;
		Active.Reset();
		return true;
	}

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
