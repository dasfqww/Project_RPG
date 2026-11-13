#include "Economy/Backend/RPGDungeonRewardSettlement.h"

#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
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

FRPGDungeonRewardSettlement::~FRPGDungeonRewardSettlement() { Cancel(); }

void FRPGDungeonRewardSettlement::Cancel()
{
	if (!ActiveRequest.IsValid()) return;
	ActiveRequest->OnProcessRequestComplete().Unbind();
	ActiveRequest->CancelRequest();
	ActiveRequest.Reset();
}

bool FRPGDungeonRewardSettlement::ClaimCompletion(const FHttpRequestPtr& Completed)
{
	if (!ActiveRequest.IsValid() || ActiveRequest != Completed) return false;
	ActiveRequest.Reset();
	return true;
}

void FRPGDungeonRewardSettlement::Settle(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	// Keep the original result authoritative while it is still in flight,
	// including when a disconnect was observed after dispatch.
	if (ActiveRequest.IsValid()) return;
	if (!Host.CanDispatchRewardSettlement())
	{
		// A local transient deferral, not a backend rejection or a failed dungeon.
		Host.BroadcastRewardSettlement(TEXT("WaitingForDisconnectionReports"), 425, false);
		return;
	}
	FHttpRequestPtr Request = CreateRequest(RewardVersion, CurrencyChanges, ItemRewards);
	if (Request.IsValid() && Request->ProcessRequest()) return;
	if (Request.IsValid())
	{
		Request->OnProcessRequestComplete().Unbind();
		if (!ClaimCompletion(Request)) return;
	}
	Host.BroadcastRewardSettlement(FString(), 0, false);
}

FHttpRequestPtr FRPGDungeonRewardSettlement::CreateRequest(
	const FString& RewardVersion, const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	const FRPGDungeonRewardSettlementConfig Config = Host.GetRewardSettlementConfig();
	const FString NormalizedRewardVersion = RewardVersion.TrimStartAndEnd();
	if (!Host.CanDispatchRewardSettlement()
		|| ActiveRequest.IsValid()
		|| !Config.bServerConfigured
		|| NormalizedRewardVersion.IsEmpty()
		|| CurrencyChanges.Num() > 16
		|| ItemRewards.Num() > 16
		|| !FRPGDungeonItemReward::HasValidPersonalLootPool(ItemRewards))
	{
		return nullptr;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("serverId"), Config.ServerId);
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
	TFunction<bool()> ScopeIsCurrent = Host.CaptureRewardSettlementScope();
	const TWeakPtr<FRPGDungeonRewardSettlement> WeakSelf = AsShared();
	ActiveRequest = Request;
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetRewardSettlementOwner(),
		[WeakSelf, ScopeIsCurrent = MoveTemp(ScopeIsCurrent), NormalizedRewardVersion]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport)
		{
			const auto Self = WeakSelf.Pin();
			if (!Self || !Self->ClaimCompletion(Completed)) return;
			if (!ScopeIsCurrent || !ScopeIsCurrent()) return;
			Self->HandleResponse(Completed, Response, bTransport, NormalizedRewardVersion);
		});
	Request->SetURL(Config.ApiUrl / FString::Printf(
		TEXT("dungeon-sessions/%s/settle-rewards"),
		*FGenericPlatformHttp::UrlEncode(Config.SessionId)));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyRewardSettlementHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(Config.TimeoutSeconds);
	return Request;
}

void FRPGDungeonRewardSettlement::HandleResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	const FString& ExpectedRewardVersion)
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
				Host.GetRewardSettlementConfig().SessionId,
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
	Host.BroadcastRewardSettlement(
		State,
		ResponseCode,
		bSuccess);
}
