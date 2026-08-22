#pragma once

#include "CoreMinimal.h"
#include "Manager/HttpWebManager.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGDungeonSessionSubsystem.generated.h"

class APlayerController;

UENUM(BlueprintType)
enum class ERPGDungeonClientFlowState : uint8
{
	Idle,
	CreatingSession,
	JoiningSession,
	InSession,
	RefreshingSession,
	RequestingJoinTicket,
	Connecting,
	LeavingSession,
	Error,
	ResumingSession
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FRPGDungeonClientFlowStateChanged,
	ERPGDungeonClientFlowState, NewState,
	const FString&, ErrorCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGDungeonClientSessionChanged,
	const FRPGDungeonSession&, DungeonSession);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGDungeonClientTravelStarted,
	const FString&, ServerAddress);

/**
 * Blueprint-facing coordinator for the client dungeon flow.
 *
 * UHttpWebManager remains the low-level HTTP transport. This subsystem owns
 * request ordering and the selected-character/current-session state so UI
 * Blueprints do not need to correlate unrelated HTTP delegates themselves.
 */
UCLASS()
class PROJECT_RPG_API URPGDungeonSessionSubsystem
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool SelectCharacter(const FString& CharacterId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool CreateDungeonSession(
		const FString& DungeonId,
		const FString& Difficulty);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool JoinDungeonSession(const FString& DungeonSessionId);

	/** Restores the selected character's active session after a restart. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ResumeDungeonSession();

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool RefreshDungeonSession();

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool LeaveDungeonSession();

	/**
	 * Requests a session-bound join ticket and travels on success.
	 * ServerAddress is expected to come from the authenticated backend dungeon
	 * session after it reaches Loading or InProgress.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ConnectToDungeon(
		APlayerController* PlayerController,
		const FString& ServerAddress);

	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	void ResetDungeonFlow(bool bKeepSelectedCharacter = true);

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	ERPGDungeonClientFlowState GetFlowState() const { return FlowState; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	const FString& GetSelectedCharacterId() const
	{
		return SelectedCharacterId;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	FRPGDungeonSession GetCurrentDungeonSession() const
	{
		return CurrentDungeonSession;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	const FString& GetLastErrorCode() const { return LastErrorCode; }

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool IsRequestInFlight() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Online|Dungeon")
	bool HasActiveDungeonSession() const;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientFlowStateChanged OnFlowStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientSessionChanged OnDungeonSessionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Online|Dungeon")
	FRPGDungeonClientTravelStarted OnDungeonTravelStarted;

private:
	enum class EPendingOperation : uint8
	{
		None,
		Create,
		Join,
		Refresh,
		Leave,
		Connect,
		Resume
	};

	UFUNCTION()
	void HandleDungeonSessionUpdated(
		const FRPGDungeonSession& DungeonSession,
		bool bSuccess);

	UFUNCTION()
	void HandleJoinTicketIssued(
		const FString& DungeonSessionId,
		const FString& CharacterId,
		const FString& JoinTicket,
		const FString& ExpiresAt,
		bool bSuccess);

	bool BeginOperation(
		EPendingOperation Operation,
		ERPGDungeonClientFlowState RequestState);
	bool ValidateSelectedCharacter();
	bool CurrentSessionContainsSelectedCharacter() const;
	void CompleteSessionOperation(const FRPGDungeonSession& DungeonSession);
	void FailOperation(const FString& ErrorCode);
	void RejectRequest(const FString& ErrorCode);
	void SetFlowState(
		ERPGDungeonClientFlowState NewState,
		const FString& ErrorCode = FString());
	void BroadcastSessionChanged();

	UPROPERTY(Transient)
	TObjectPtr<UHttpWebManager> WebManager;

	UPROPERTY(Transient)
	FRPGDungeonSession CurrentDungeonSession;

	FString SelectedCharacterId;
	FString LastErrorCode;
	FString PendingServerAddress;
	TWeakObjectPtr<APlayerController> PendingPlayerController;
	EPendingOperation PendingOperation = EPendingOperation::None;
	ERPGDungeonClientFlowState FlowState =
		ERPGDungeonClientFlowState::Idle;
};
