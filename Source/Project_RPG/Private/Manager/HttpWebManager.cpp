#include "Manager/HttpWebManager.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
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

	bool TryParseBackendCharacter(
		const TSharedPtr<FJsonObject>& Object,
		FRPGBackendCharacter& OutCharacter)
	{
		return Object.IsValid()
			&& Object->TryGetStringField(
				TEXT("characterId"), OutCharacter.CharacterId)
			&& Object->TryGetStringField(
				TEXT("rosterId"), OutCharacter.RosterId)
			&& Object->TryGetStringField(TEXT("name"), OutCharacter.Name)
			&& Object->TryGetStringField(
				TEXT("createdAt"), OutCharacter.CreatedAt)
			&& !OutCharacter.CharacterId.IsEmpty()
			&& !OutCharacter.RosterId.IsEmpty()
			&& !OutCharacter.Name.IsEmpty();
	}

	bool TryParseDungeonSession(
		const TSharedPtr<FJsonObject>& Object,
		FRPGDungeonSession& OutSession)
	{
		if (!Object.IsValid()
			|| !Object->TryGetStringField(
				TEXT("dungeonSessionId"), OutSession.DungeonSessionId)
			|| !Object->TryGetStringField(
				TEXT("dungeonId"), OutSession.DungeonId)
			|| !Object->TryGetStringField(
				TEXT("difficulty"), OutSession.Difficulty)
			|| !Object->TryGetStringField(TEXT("state"), OutSession.State)
			|| !Object->TryGetStringField(
				TEXT("expiresAt"), OutSession.ExpiresAt)
			|| OutSession.DungeonSessionId.IsEmpty())
		{
			return false;
		}

		Object->TryGetStringField(TEXT("serverId"), OutSession.ServerId);
		Object->TryGetStringField(
			TEXT("serverAddress"),
			OutSession.ServerAddress);

		const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
		if (!Object->TryGetArrayField(TEXT("members"), MemberValues)
			|| !MemberValues)
		{
			return false;
		}

		OutSession.Members.Reset(MemberValues->Num());
		for (const TSharedPtr<FJsonValue>& MemberValue : *MemberValues)
		{
			const TSharedPtr<FJsonObject> MemberObject =
				MemberValue.IsValid() ? MemberValue->AsObject() : nullptr;
			FRPGDungeonSessionMember Member;
			if (!MemberObject.IsValid()
				|| !MemberObject->TryGetStringField(
					TEXT("characterId"), Member.CharacterId)
				|| !MemberObject->TryGetStringField(
					TEXT("joinedAt"), Member.JoinedAt)
				|| !MemberObject->TryGetStringField(
					TEXT("leaseExpiresAt"), Member.LeaseExpiresAt)
				|| Member.CharacterId.IsEmpty())
			{
				OutSession = FRPGDungeonSession();
				return false;
			}

			OutSession.Members.Add(MoveTemp(Member));
		}

		return true;
	}
}

void UHttpWebManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IsRunningDedicatedServer())
	{
		BackendServiceToken = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).TrimStartAndEnd();
		GameServerId = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_GAME_SERVER_ID")).TrimStartAndEnd();
		ConfiguredDungeonSessionId = FPlatformMisc::GetEnvironmentVariable(
			TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).TrimStartAndEnd();
		if (BackendServiceToken.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Dedicated Server backend game-server token is not configured. "
					"Persistence requests may be rejected."));
		}
		if (GameServerId.IsEmpty() || ConfiguredDungeonSessionId.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Dedicated Server dungeon assignment is incomplete. Set "
					"PROJECT_RPG_GAME_SERVER_ID and "
					"PROJECT_RPG_DUNGEON_SESSION_ID."));
		}
	}
	UE_LOG(LogTemp, Log, TEXT("HttpWebManager initialized."));
}

void UHttpWebManager::Deinitialize()
{
	OnInventoryLoaded.Clear();
	OnCharacterInventoryLoaded.Clear();
	OnInventorySaved.Clear();
	OnCharacterInventorySaved.Clear();
	OnBackendAuthenticationCompleted.Clear();
	OnCharactersLoaded.Clear();
	OnCharacterCreated.Clear();
	OnDungeonSessionUpdated.Clear();
	OnJoinTicketIssued.Clear();
	OnJoinTicketConsumed.Clear();
	OnDungeonRewardSettlementRequested.Clear();
	AccessToken.Reset();
	BackendServiceToken.Reset();
	GameServerId.Reset();
	ConfiguredDungeonSessionId.Reset();
	Super::Deinitialize();
}

void UHttpWebManager::SetAccessToken(const FString& InAccessToken)
{
	AccessToken = InAccessToken.TrimStartAndEnd();
}

void UHttpWebManager::ClearAccessToken()
{
	AccessToken.Reset();
}

void UHttpWebManager::ApplyCommonHeaders(const FHttpRequestRef& Request) const
{
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	const FString& BearerToken = AccessToken.IsEmpty()
		? BackendServiceToken
		: AccessToken;
	if (!BearerToken.IsEmpty())
	{
		Request->SetHeader(
			TEXT("Authorization"),
			FString::Printf(TEXT("Bearer %s"), *BearerToken));
	}
}

void UHttpWebManager::AuthenticateWithSteamTicket(const FString& SteamTicketHex)
{
	if (SteamTicketHex.IsEmpty())
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("ticket"), SteamTicketHex);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this, &ThisClass::OnAuthenticateResponseReceived);
	Request->SetURL(ApiUrl / TEXT("auth/steam-ticket"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::LoadCharacters()
{
	if (AccessToken.IsEmpty())
	{
		const TArray<FRPGBackendCharacter> EmptyCharacters;
		OnCharactersLoaded.Broadcast(EmptyCharacters, false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnLoadCharactersResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::CreateCharacter(const FString& CharacterName)
{
	const FString NormalizedName = CharacterName.TrimStartAndEnd();
	if (AccessToken.IsEmpty() || NormalizedName.IsEmpty())
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("name"), NormalizedName);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnCreateCharacterResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::CreateDungeonSession(
	const FString& CharacterId,
	const FString& DungeonId,
	const FString& Difficulty)
{
	const FString NormalizedDungeonId = DungeonId.TrimStartAndEnd();
	const FString NormalizedDifficulty = Difficulty.TrimStartAndEnd();
	if (AccessToken.IsEmpty()
		|| CharacterId.IsEmpty()
		|| NormalizedDungeonId.IsEmpty()
		|| NormalizedDifficulty.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	RootObject->SetStringField(TEXT("dungeonId"), NormalizedDungeonId);
	RootObject->SetStringField(TEXT("difficulty"), NormalizedDifficulty);
	SendDungeonSessionRequest(
		TEXT("dungeon-sessions"),
		TEXT("POST"),
		RootObject,
		TEXT("create"),
		FString());
}

void UHttpWebManager::JoinDungeonSession(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (AccessToken.IsEmpty()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("POST"),
		RootObject,
		TEXT("join"),
		DungeonSessionId);
}

void UHttpWebManager::LoadDungeonSession(const FString& DungeonSessionId)
{
	if (AccessToken.IsEmpty() || DungeonSessionId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("GET"),
		nullptr,
		TEXT("load"),
		DungeonSessionId);
}

void UHttpWebManager::LoadActiveDungeonSession(
	const FString& CharacterId)
{
	const FString NormalizedCharacterId = CharacterId.TrimStartAndEnd();
	if (AccessToken.IsEmpty() || NormalizedCharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("characters/%s/active-dungeon-session"),
			*FGenericPlatformHttp::UrlEncode(NormalizedCharacterId)),
		TEXT("GET"),
		nullptr,
		TEXT("resume"),
		FString());
}

void UHttpWebManager::LeaveDungeonSession(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (AccessToken.IsEmpty()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId),
			*FGenericPlatformHttp::UrlEncode(CharacterId)),
		TEXT("DELETE"),
		nullptr,
		TEXT("leave"),
		DungeonSessionId);
}

void UHttpWebManager::RequestJoinTicket(
	const FString& CharacterId,
	const FString& DungeonSessionId)
{
	if (CharacterId.IsEmpty()
		|| DungeonSessionId.IsEmpty()
		|| AccessToken.IsEmpty())
	{
		OnJoinTicketIssued.Broadcast(
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
		OnJoinTicketIssued.Broadcast(
			DungeonSessionId,
			CharacterId,
			FString(),
			FString(),
			false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnRequestJoinTicketResponseReceived,
		CharacterId,
		DungeonSessionId);
	Request->SetURL(ApiUrl / TEXT("join-tickets"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::ConsumeJoinTicket(const FString& JoinTicket)
{
	if (!IsRunningDedicatedServer()
		|| JoinTicket.IsEmpty()
		|| BackendServiceToken.IsEmpty()
		|| GameServerId.IsEmpty()
		|| ConfiguredDungeonSessionId.IsEmpty())
	{
		OnJoinTicketConsumed.Broadcast(
			JoinTicket,
			FString(),
			FString(),
			FString(),
			false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("joinTicket"), JoinTicket);
	RootObject->SetStringField(TEXT("serverId"), GameServerId);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnJoinTicketConsumed.Broadcast(
			JoinTicket,
			FString(),
			FString(),
			FString(),
			false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnConsumeJoinTicketResponseReceived,
		JoinTicket);
	Request->SetURL(ApiUrl / TEXT("join-tickets/consume"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
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

void UHttpWebManager::SettleConfiguredDungeonRewards(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	const FString NormalizedRewardVersion = RewardVersion.TrimStartAndEnd();
	if (!IsRunningDedicatedServer()
		|| !IsConfiguredForDungeonServer()
		|| NormalizedRewardVersion.IsEmpty()
		|| CurrencyChanges.Num() > 16
		|| ItemRewards.Num() > 16)
	{
		OnDungeonRewardSettlementRequested.Broadcast(FString(), 0, false);
		return;
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
			OnDungeonRewardSettlementRequested.Broadcast(
				FString(),
				0,
				false);
			return;
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
			|| !Reward.Durability.IsValid())
		{
			OnDungeonRewardSettlementRequested.Broadcast(
				FString(),
				0,
				false);
			return;
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
				OnDungeonRewardSettlementRequested.Broadcast(
					FString(),
					0,
					false);
				return;
			}

			TSharedRef<FJsonObject> StatObject = MakeShared<FJsonObject>();
			StatObject->SetStringField(
				TEXT("statTag"),
				Stat.StatTag.ToString());
			StatObject->SetNumberField(TEXT("value"), Stat.Value);
			StatValues.Add(MakeShared<FJsonValueObject>(StatObject));
		}
		RewardObject->SetArrayField(TEXT("statValues"), StatValues);
		ItemRewardValues.Add(MakeShared<FJsonValueObject>(RewardObject));
	}
	RootObject->SetArrayField(TEXT("itemRewards"), ItemRewardValues);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnDungeonRewardSettlementRequested.Broadcast(FString(), 0, false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnSettleDungeonRewardsResponseReceived,
		NormalizedRewardVersion);
	Request->SetURL(ApiUrl / FString::Printf(
		TEXT("dungeon-sessions/%s/settle-rewards"),
		*FGenericPlatformHttp::UrlEncode(ConfiguredDungeonSessionId)));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::SendConfiguredDungeonSessionRequest(
	const FString& Action,
	const FString& Outcome)
{
	if (!IsRunningDedicatedServer()
		|| BackendServiceToken.IsEmpty()
		|| GameServerId.IsEmpty()
		|| ConfiguredDungeonSessionId.IsEmpty())
	{
		OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("serverId"), GameServerId);
	if (!Outcome.IsEmpty())
	{
		RootObject->SetStringField(TEXT("outcome"), Outcome);
	}

	SendDungeonSessionRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/%s"),
			*FGenericPlatformHttp::UrlEncode(ConfiguredDungeonSessionId),
			*Action),
		TEXT("POST"),
		RootObject,
		Action,
		ConfiguredDungeonSessionId);
}

void UHttpWebManager::SendDungeonSessionRequest(
	const FString& RelativePath,
	const FString& Verb,
	const TSharedPtr<FJsonObject>& RequestObject,
	const FString& Operation,
	const FString& ExpectedDungeonSessionId)
{
	FString RequestBody;
	if (RequestObject.IsValid())
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
			Writer = TJsonWriterFactory<
				TCHAR,
				TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
		if (!FJsonSerializer::Serialize(RequestObject.ToSharedRef(), Writer))
		{
			OnDungeonSessionUpdated.Broadcast(FRPGDungeonSession(), false);
			return;
		}
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnDungeonSessionResponseReceived,
		Operation,
		ExpectedDungeonSessionId);
	Request->SetURL(ApiUrl / RelativePath);
	Request->SetVerb(Verb);
	if (RequestObject.IsValid())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(RequestBody);
	}
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

bool UHttpWebManager::ConnectWithJoinTicket(
	APlayerController* PlayerController,
	const FString& ServerAddress,
	const FString& JoinTicket)
{
	if (!IsValid(PlayerController)
		|| ServerAddress.IsEmpty()
		|| JoinTicket.IsEmpty())
	{
		return false;
	}

	const FString EncodedTicket =
		FGenericPlatformHttp::UrlEncode(JoinTicket);
	const FString TravelUrl = FString::Printf(
		TEXT("%s?JoinTicket=%s"),
		*ServerAddress,
		*EncodedTicket);
	PlayerController->ClientTravel(TravelUrl, TRAVEL_Absolute);
	return true;
}

void UHttpWebManager::SaveInventoryToWeb(
	const TArray<FItemSaveData>& InventoryData, const FString& CharacterId)
{
	if (CharacterId.IsEmpty())
	{
		OnInventorySaved.Broadcast(false);
		OnCharacterInventorySaved.Broadcast(CharacterId, false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);

	TArray<TSharedPtr<FJsonValue>> InventoryJsonArray;
	InventoryJsonArray.Reserve(InventoryData.Num());
	for (const FItemSaveData& Item : InventoryData)
	{
		TSharedRef<FJsonObject> ItemObject = MakeShared<FJsonObject>();
		ItemObject->SetStringField(TEXT("item_id"), Item.ItemID.ToString());
		ItemObject->SetNumberField(TEXT("quantity"), Item.Quantity);
		ItemObject->SetNumberField(TEXT("slot_index"), Item.SlotIndex);
		ItemObject->SetStringField(TEXT("category"), Item.Category);
		ItemObject->SetStringField(TEXT("instance_id"), Item.InstanceId);
		InventoryJsonArray.Add(MakeShared<FJsonValueObject>(ItemObject));
	}
	RootObject->SetArrayField(TEXT("inventory"), InventoryJsonArray);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnInventorySaved.Broadcast(false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this, &ThisClass::OnSaveInventoryResponseReceived, CharacterId);
	Request->SetURL(ApiUrl / TEXT("saveInventory"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Verbose,
		TEXT("Saving %d inventory entries for character %s."),
		InventoryData.Num(), *CharacterId);
}

void UHttpWebManager::LoadInventoryFromWeb(const FString& CharacterId)
{
	if (CharacterId.IsEmpty())
	{
		const TArray<FItemSaveData> EmptyData;
		OnInventoryLoaded.Broadcast(EmptyData);
		OnCharacterInventoryLoaded.Broadcast(CharacterId, EmptyData, false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this, &ThisClass::OnLoadInventoryResponseReceived, CharacterId);

	const FString EncodedCharacterId = FGenericPlatformHttp::UrlEncode(CharacterId);
	Request->SetURL(FString::Printf(
		TEXT("%s/loadInventory?characterId=%s"), *ApiUrl, *EncodedCharacterId));
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();

	UE_LOG(LogTemp, Verbose,
		TEXT("Loading inventory for character %s."), *CharacterId);
}

void UHttpWebManager::OnSaveInventoryResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString CharacterId)
{
	const bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Inventory save failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnInventorySaved.Broadcast(bSuccess);
	OnCharacterInventorySaved.Broadcast(CharacterId, bSuccess);
}

void UHttpWebManager::OnLoadInventoryResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString CharacterId)
{
	TArray<FItemSaveData> LoadedData;
	bool bSuccess = false;

	if (bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode()))
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());

		if (FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* InventoryArray = nullptr;
			if (RootObject->TryGetArrayField(TEXT("inventory"), InventoryArray)
				&& InventoryArray)
			{
				LoadedData.Reserve(InventoryArray->Num());
				for (const TSharedPtr<FJsonValue>& Value : *InventoryArray)
				{
					const TSharedPtr<FJsonObject> ItemObject =
						Value.IsValid() ? Value->AsObject() : nullptr;
					if (!ItemObject.IsValid())
					{
						continue;
					}

					FString ItemId;
					double Quantity = 0;
					double SlotIndex = INDEX_NONE;
					if (!ItemObject->TryGetStringField(TEXT("item_id"), ItemId)
						|| !ItemObject->TryGetNumberField(TEXT("quantity"), Quantity)
						|| !ItemObject->TryGetNumberField(TEXT("slot_index"), SlotIndex))
					{
						continue;
					}

					FItemSaveData& ItemData = LoadedData.AddDefaulted_GetRef();
					ItemData.ItemID = FName(*ItemId);
					ItemData.Quantity = FMath::TruncToInt(Quantity);
					ItemData.SlotIndex = FMath::TruncToInt(SlotIndex);
					ItemObject->TryGetStringField(TEXT("category"), ItemData.Category);
					ItemObject->TryGetStringField(
						TEXT("instance_id"), ItemData.InstanceId);
				}
			}

			bSuccess = true;
		}
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Inventory load failed for character %s (HTTP %d)."),
			*CharacterId, Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnInventoryLoaded.Broadcast(LoadedData);
	OnCharacterInventoryLoaded.Broadcast(CharacterId, LoadedData, bSuccess);
}

void UHttpWebManager::OnAuthenticateResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString NewAccessToken;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(TEXT("accessToken"), NewAccessToken)
			&& !NewAccessToken.IsEmpty();

		if (bSuccess)
		{
			SetAccessToken(NewAccessToken);
		}
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Steam backend authentication failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnBackendAuthenticationCompleted.Broadcast(bSuccess);
}

void UHttpWebManager::OnLoadCharactersResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	TArray<FRPGBackendCharacter> Characters;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TArray<TSharedPtr<FJsonValue>> CharacterValues;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, CharacterValues);
		if (bSuccess)
		{
			Characters.Reserve(CharacterValues.Num());
			for (const TSharedPtr<FJsonValue>& CharacterValue : CharacterValues)
			{
				FRPGBackendCharacter Character;
				if (!CharacterValue.IsValid()
					|| CharacterValue->Type != EJson::Object
					|| !TryParseBackendCharacter(
						CharacterValue->AsObject(),
						Character))
				{
					bSuccess = false;
					Characters.Reset();
					break;
				}
				Characters.Add(MoveTemp(Character));
			}
		}
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character list request failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharactersLoaded.Broadcast(Characters, bSuccess);
}

void UHttpWebManager::OnCreateCharacterResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	FRPGBackendCharacter Character;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& TryParseBackendCharacter(RootObject, Character);
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character creation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharacterCreated.Broadcast(Character, bSuccess);
}

void UHttpWebManager::OnDungeonSessionResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString Operation,
	FString ExpectedDungeonSessionId)
{
	FRPGDungeonSession DungeonSession;
	const int32 ResponseCode = Response.IsValid()
		? Response->GetResponseCode()
		: 0;
	const bool bNoActiveSession = bWasSuccessful
		&& ResponseCode == EHttpResponseCodes::NoContent
		&& Operation.Equals(TEXT("resume"), ESearchCase::CaseSensitive);
	bool bSuccess = bNoActiveSession
		|| (bWasSuccessful && Response.IsValid()
			&& EHttpResponseCodes::IsOk(ResponseCode));

	if (bSuccess && !bNoActiveSession)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& TryParseDungeonSession(RootObject, DungeonSession)
			&& (ExpectedDungeonSessionId.IsEmpty()
				|| DungeonSession.DungeonSessionId.Equals(
					ExpectedDungeonSessionId,
					ESearchCase::CaseSensitive));
	}

	if (!bSuccess)
	{
		DungeonSession = FRPGDungeonSession();
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon session %s request failed (HTTP %d)."),
			*Operation,
			ResponseCode);
	}

	OnDungeonSessionUpdated.Broadcast(DungeonSession, bSuccess);
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

void UHttpWebManager::OnRequestJoinTicketResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString CharacterId,
	FString DungeonSessionId)
{
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	FString JoinTicket;
	FString ExpiresAt;

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
		UE_LOG(LogTemp, Error,
			TEXT("Join ticket request failed for character %s (HTTP %d)."),
			*CharacterId,
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnJoinTicketIssued.Broadcast(
		DungeonSessionId,
		CharacterId,
		JoinTicket,
		ExpiresAt,
		bSuccess);
}

void UHttpWebManager::OnConsumeJoinTicketResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	FString JoinTicket)
{
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	FString DungeonSessionId;
	FString CharacterId;
	FString SteamId;

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(
				TEXT("dungeonSessionId"), DungeonSessionId)
			&& RootObject->TryGetStringField(TEXT("characterId"), CharacterId)
			&& RootObject->TryGetStringField(TEXT("steamId"), SteamId)
			&& !DungeonSessionId.IsEmpty()
			&& !CharacterId.IsEmpty()
			&& !SteamId.IsEmpty();
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Join ticket validation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnJoinTicketConsumed.Broadcast(
		JoinTicket,
		DungeonSessionId,
		CharacterId,
		SteamId,
		bSuccess);
}
