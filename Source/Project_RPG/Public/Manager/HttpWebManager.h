#pragma once

#include "CoreMinimal.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Http.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Type/RPGStructTypes.h"
#include "HttpWebManager.generated.h"

class APlayerController;
class FJsonObject;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBackendCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString RosterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Character")
	FString CreatedAt;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSessionMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString JoinedAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString LeaseExpiresAt;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString Difficulty;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerAddress;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ExpiresAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	TArray<FRPGDungeonSessionMember> Members;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventorySaved, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharacterInventorySaved,
	const FString&, CharacterId,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnInventoryLoaded, const TArray<FItemSaveData>&, LoadedData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnCharacterInventoryLoaded,
	const FString&, CharacterId,
	const TArray<FItemSaveData>&, LoadedData,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnBackendAuthenticationCompleted,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharactersLoaded,
	const TArray<FRPGBackendCharacter>&, Characters,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnCharacterCreated,
	const FRPGBackendCharacter&, Character,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnDungeonSessionUpdated,
	const FRPGDungeonSession&, DungeonSession,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnJoinTicketIssued,
	const FString&, DungeonSessionId,
	const FString&, CharacterId,
	const FString&, JoinTicket,
	const FString&, ExpiresAt,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FOnDungeonRewardSettlementRequested,
	const FString&, State,
	int32, HttpStatus,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(
	FOnJoinTicketConsumed,
	const FString&, JoinTicket,
	const FString&, DungeonSessionId,
	const FString&, CharacterId,
	const FString&, SteamId,
	bool, bSuccess);

/**
 * Thin HTTP transport used by the authoritative game server.
 * Authentication and database transactions still belong to the backend service.
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API UHttpWebManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void SaveInventoryToWeb(
		const TArray<FItemSaveData>& InventoryData, const FString& CharacterId);
	void LoadInventoryFromWeb(const FString& CharacterId);

	/**
	 * Exchanges a Steam Web API auth ticket (hex encoded) for a backend token.
	 * Ticket creation remains in the Steam/OnlineSubsystem integration layer.
	 */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void AuthenticateWithSteamTicket(const FString& SteamTicketHex);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void SetAccessToken(const FString& InAccessToken);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Authentication")
	void ClearAccessToken();

	UFUNCTION(BlueprintCallable, Category = "HTTP|Character")
	void LoadCharacters();

	UFUNCTION(BlueprintCallable, Category = "HTTP|Character")
	void CreateCharacter(const FString& CharacterName);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void CreateDungeonSession(
		const FString& CharacterId,
		const FString& DungeonId,
		const FString& Difficulty);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void JoinDungeonSession(
		const FString& DungeonSessionId,
		const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LoadDungeonSession(const FString& DungeonSessionId);

	/** Loads the selected character's current resumable dungeon, if any. */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LoadActiveDungeonSession(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void LeaveDungeonSession(
		const FString& DungeonSessionId,
		const FString& CharacterId);

	/**
	 * Requests a short-lived, single-use ticket for a character that currently
	 * holds an active lease in the specified dungeon session.
	 */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	void RequestJoinTicket(
		const FString& CharacterId,
		const FString& DungeonSessionId);

	/** Dedicated Server only: atomically consumes a client join ticket. */
	void ConsumeJoinTicket(const FString& JoinTicket);

	/** Dedicated Server only: reports that the configured instance is ready. */
	void StartConfiguredDungeonSession();

	/** Dedicated Server only: renews the configured instance and member leases. */
	void HeartbeatConfiguredDungeonSession();

	/** Dedicated Server only: reports the authoritative PvE result. */
	void FinishConfiguredDungeonSession(bool bCleared);

	/** Dedicated Server only: persists and queues the authoritative clear reward. */
	void SettleConfiguredDungeonRewards(
		const FString& RewardVersion,
		const TArray<FRPGCurrencyChange>& CurrencyChanges,
		const TArray<FRPGDungeonItemReward>& ItemRewards);

	/** Appends the ticket as a URL option and starts the UE connection. */
	UFUNCTION(BlueprintCallable, Category = "HTTP|Dungeon Session")
	static bool ConnectWithJoinTicket(
		APlayerController* PlayerController,
		const FString& ServerAddress,
		const FString& JoinTicket);

	UFUNCTION(BlueprintPure, Category = "HTTP|Authentication")
	bool HasAccessToken() const { return !AccessToken.IsEmpty(); }

	const FString& GetGameServerId() const { return GameServerId; }
	const FString& GetConfiguredDungeonSessionId() const
	{
		return ConfiguredDungeonSessionId;
	}
	bool IsConfiguredForDungeonServer() const
	{
		return !BackendServiceToken.IsEmpty()
			&& !GameServerId.IsEmpty()
			&& !ConfiguredDungeonSessionId.IsEmpty();
	}

	void OnSaveInventoryResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString CharacterId);
	void OnLoadInventoryResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString CharacterId);
	void OnAuthenticateResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnLoadCharactersResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnCreateCharacterResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful);
	void OnDungeonSessionResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString Operation,
		FString ExpectedDungeonSessionId);
	void OnRequestJoinTicketResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString CharacterId,
		FString DungeonSessionId);
	void OnConsumeJoinTicketResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString JoinTicket);
	void OnSettleDungeonRewardsResponseReceived(
		FHttpRequestPtr Request,
		FHttpResponsePtr Response,
		bool bWasSuccessful,
		FString ExpectedRewardVersion);
	void ApplyCommonHeaders(const FHttpRequestRef& Request) const;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventorySaved OnInventorySaved;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnCharacterInventorySaved OnCharacterInventorySaved;

	/** Legacy uncorrelated callback. Prefer OnCharacterInventoryLoaded. */
	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnInventoryLoaded OnInventoryLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP")
	FOnCharacterInventoryLoaded OnCharacterInventoryLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Authentication")
	FOnBackendAuthenticationCompleted OnBackendAuthenticationCompleted;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Character")
	FOnCharactersLoaded OnCharactersLoaded;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Character")
	FOnCharacterCreated OnCharacterCreated;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Dungeon Session")
	FOnDungeonSessionUpdated OnDungeonSessionUpdated;

	UPROPERTY(BlueprintAssignable, Category = "HTTP|Dungeon Session")
	FOnJoinTicketIssued OnJoinTicketIssued;

	UPROPERTY()
	FOnJoinTicketConsumed OnJoinTicketConsumed;

	UPROPERTY()
	FOnDungeonRewardSettlementRequested OnDungeonRewardSettlementRequested;

private:
	UPROPERTY(Config, EditAnywhere, Category = "HTTP")
	FString ApiUrl = TEXT("http://localhost:3000/api");

	UPROPERTY(Config, EditAnywhere, Category = "HTTP", meta = (ClampMin = "1.0"))
	float RequestTimeoutSeconds = 10.f;

	/** Kept in memory only; never persist this token in a packaged client config. */
	FString AccessToken;

	/** Loaded from PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN on a dedicated server only. */
	FString BackendServiceToken;

	/** Stable ID assigned to this process by the game-server allocator. */
	FString GameServerId;

	/** Dungeon instance assigned to this process by the game-server allocator. */
	FString ConfiguredDungeonSessionId;

	void SendDungeonSessionRequest(
		const FString& RelativePath,
		const FString& Verb,
		const TSharedPtr<FJsonObject>& RequestObject,
		const FString& Operation,
		const FString& ExpectedDungeonSessionId);

	void SendConfiguredDungeonSessionRequest(
		const FString& Action,
		const FString& Outcome = FString());
};
