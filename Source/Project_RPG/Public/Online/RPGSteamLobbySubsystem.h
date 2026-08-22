#pragma once

#include "CoreMinimal.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGSteamLobbySubsystem.generated.h"

class APlayerController;
class FOnlineSession;
class FOnlineSessionSearch;
class URPGDungeonSessionSubsystem;
enum class ERPGDungeonClientFlowState : uint8;

UENUM(BlueprintType)
enum class ERPGSteamLobbyState : uint8
{
	Idle,
	CreatingLobby,
	InLobby,
	Searching,
	JoiningLobby,
	JoiningBackendSession,
	UpdatingLobby,
	LeavingBackendSession,
	LeavingLobby,
	Error
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGSteamLobbyInfo
{
	GENERATED_BODY()

	/** Index accepted by JoinPartyLobbyByIndex. -1 for the current lobby. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	int32 ResultIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString LobbyId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString OwnerName;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString DungeonSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString DungeonId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString Difficulty;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString DungeonState;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString ServerId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString ServerAddress;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	FString BuildId;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	int32 CurrentPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	int32 MaxPlayers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	int32 PingInMs = 0;

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Online|Lobby")
	bool bJoinable = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FRPGSteamLobbyStateChanged,
	ERPGSteamLobbyState, NewState,
	const FString&, ErrorCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FRPGSteamLobbySearchCompleted,
	const TArray<FRPGSteamLobbyInfo>&, Results,
	bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSteamLobbyChanged,
	const FRPGSteamLobbyInfo&, Lobby);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGSteamLobbyInviteAccepted,
	const FRPGSteamLobbyInfo&, Lobby);

/**
 * Steam party-lobby coordinator for small-session PvE.
 *
 * Lobby metadata is discovery data only. Membership and character leases are
 * still authorized by URPGDungeonSessionSubsystem and the backend.
 */
UCLASS()
class PROJECT_RPG_API URPGSteamLobbySubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * Creates a Steam lobby for the current backend dungeon session.
	 * CreateDungeonSession must have completed first.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool CreatePartyLobby(int32 MaxPlayers = 4);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool FindPartyLobbies(int32 MaxResults = 50);

	/** Opens the Steam friends invite overlay for the current party lobby. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool ShowPartyInviteOverlay();

	/**
	 * Joins the Steam lobby, then joins its backend dungeon session.
	 * ResultIndex comes from OnLobbySearchCompleted.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool JoinPartyLobbyByIndex(int32 ResultIndex);

	/**
	 * Joins a Steam invite previously delivered through
	 * OnPartyInviteAccepted. This can be called after login and character
	 * selection finish, so startup invites are not lost.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool AcceptPendingPartyInvite();

	/**
	 * Publishes the backend-verified allocator output after the session becomes
	 * connectable.
	 * Only the Steam lobby owner can update this metadata.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool PublishDungeonServerAddress();

	/**
	 * Requests a one-use backend ticket, then travels to the address currently
	 * stored in the backend dungeon session. Lobby metadata must identify the
	 * same backend assignment but is never the authority for the address.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool ConnectToDungeonServer(APlayerController* PlayerController);

	/**
	 * Leaves the backend Waiting session first, then the Steam lobby.
	 * Active/loading dungeon sessions intentionally cannot be abandoned through
	 * this party-lobby operation.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Lobby")
	bool LeavePartyLobby();

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	ERPGSteamLobbyState GetLobbyState() const { return LobbyState; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	const FString& GetLastErrorCode() const { return LastErrorCode; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	FRPGSteamLobbyInfo GetCurrentLobby() const { return CurrentLobby; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	FRPGSteamLobbyInfo GetPendingPartyInvite() const
	{
		return PendingInviteLobby;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	bool IsInPartyLobby() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	bool HasPendingPartyInvite() const
	{
		return PendingInviteResult.IsValid();
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Lobby")
	bool IsLobbyRequestInFlight() const;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Lobby")
	FRPGSteamLobbyStateChanged OnLobbyStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Lobby")
	FRPGSteamLobbySearchCompleted OnLobbySearchCompleted;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Lobby")
	FRPGSteamLobbyChanged OnLobbyChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Lobby")
	FRPGSteamLobbyInviteAccepted OnPartyInviteAccepted;

private:
	enum class EPendingOperation : uint8
	{
		None,
		Create,
		Find,
		JoinSteam,
		JoinBackend,
		Update,
		LeaveBackend,
		Destroy
	};

	bool EnsureSessionInterface();
	void RegisterSessionDelegates();
	void ClearSessionDelegates();
	bool BeginOperation(
		EPendingOperation Operation,
		ERPGSteamLobbyState RequestState);
	bool StartJoinPartyLobby(
		const FOnlineSessionSearchResult& SearchResult,
		int32 LocalUserNum,
		int32 ResultIndex);
	bool BeginDestroyLobby(const FString& ErrorAfterDestroy = FString());
	void FailOperation(const FString& ErrorCode);
	void RejectRequest(const FString& ErrorCode);
	void SetLobbyState(
		ERPGSteamLobbyState NewState,
		const FString& ErrorCode = FString());
	void ClearCurrentLobby();
	void RefreshCurrentLobbyFromNamedSession();
	FRPGSteamLobbyInfo BuildLobbyInfo(
		const FOnlineSession& Session,
		int32 ResultIndex,
		int32 PingInMs) const;

	void HandleCreateSessionComplete(FName SessionName, bool bSuccess);
	void HandleFindSessionsComplete(bool bSuccess);
	void HandleJoinSessionComplete(
		FName SessionName,
		EOnJoinSessionCompleteResult::Type Result);
	void HandleUpdateSessionComplete(FName SessionName, bool bSuccess);
	void HandleDestroySessionComplete(FName SessionName, bool bSuccess);
	void HandleSessionUserInviteAccepted(
		bool bSuccess,
		int32 ControllerId,
		FUniqueNetIdPtr UserId,
		const FOnlineSessionSearchResult& InviteResult);

	UFUNCTION()
	void HandleDungeonFlowStateChanged(
		ERPGDungeonClientFlowState NewState,
		const FString& ErrorCode);

	UPROPERTY(Transient)
	TObjectPtr<URPGDungeonSessionSubsystem> DungeonFlow;

	UPROPERTY(Transient)
	FRPGSteamLobbyInfo CurrentLobby;

	UPROPERTY(Transient)
	TArray<FRPGSteamLobbyInfo> LastSearchResults;

	UPROPERTY(Transient)
	FRPGSteamLobbyInfo PendingInviteLobby;

	IOnlineSessionPtr SessionInterface;
	TSharedPtr<FOnlineSessionSearch> SessionSearch;
	TSharedPtr<FOnlineSessionSearchResult> PendingInviteResult;
	TArray<int32> VisibleSearchResultIndices;

	FDelegateHandle CreateSessionDelegateHandle;
	FDelegateHandle FindSessionsDelegateHandle;
	FDelegateHandle JoinSessionDelegateHandle;
	FDelegateHandle UpdateSessionDelegateHandle;
	FDelegateHandle DestroySessionDelegateHandle;
	FDelegateHandle InviteAcceptedDelegateHandle;

	FString PendingDungeonSessionId;
	FString ErrorAfterDestroy;
	FString LastErrorCode;
	EPendingOperation PendingOperation = EPendingOperation::None;
	ERPGSteamLobbyState LobbyState = ERPGSteamLobbyState::Idle;
	bool bDelegatesRegistered = false;
	int32 PendingInviteLocalUserNum = 0;
};
