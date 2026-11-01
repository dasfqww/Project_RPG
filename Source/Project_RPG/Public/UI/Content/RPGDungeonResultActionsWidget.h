#pragma once

#include "Blueprint/UserWidget.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "RPGDungeonResultActionsWidget.generated.h"

class UButton;
class UTextBlock;
class ARPGPlayerState;
class URPGSteamAuthSubsystem;
class ARPGPlayerController;
class UVerticalBox;

/** Presentation only. Backend terminal-state checks remain in the dungeon flow. */
struct PROJECT_RPG_API FRPGDungeonResultActionsPresentation
{
	bool bSessionMatches = false;
	bool bCanReturn = false;
	bool bCanRefresh = false;
	bool bCanAuthenticate = false;
	bool bCanDisconnect = false;
	bool bCanRetry = false;
	FText Status;
	FText RewardSummary;
};

/** Native online result controls layered over the existing victory/defeat art. */
UCLASS()
class PROJECT_RPG_API URPGDungeonResultActionsWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	void InitializeResult(bool bVictory);
	/** Reuses only the confirmation controls, never result/reward/retry actions. */
	void InitializeGameplayAbandonment();
	bool OpenGameplayAbandonmentConfirmation();
	FText GetDisplayedStatus() const;
	FText GetDisplayedRewardSummary() const;
	static FRPGDungeonResultActionsPresentation BuildPresentation(
		const FRPGDungeonSession& Session, const FString& SelectedCharacterId,
		const FString& AuthenticatedCharacterId, const FString& AuthenticatedSessionId,
		const FRPGDungeonRewardDeliveryNotice& Notice, ERPGDungeonClientFlowState FlowState,
		bool bRequestInFlight, const FString& ErrorCode, bool bPollingExpired,
		bool bBackendAuthenticated = true, bool bAuthenticationRequest = false, bool bAccountMatches = true,
		bool bDefeatedResult = false, bool bRetryRequest = false);
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	friend struct FRPGDungeonAbandonmentTestAccess;
	friend class FRPGDungeonResultNavigationTest;
	UFUNCTION()
	void HandleAbandonClicked();
	UFUNCTION()
	void HandleConfirmAbandonClicked();
	UFUNCTION()
	void HandleCancelAbandonClicked();
	bool CanOpenAbandonment() const;
	bool CanConfirmAbandonment() const;
	void CloseAbandonmentConfirmation();
	UFUNCTION()
	void HandleRetryClicked();
	UFUNCTION()
	void HandleRetryRequestChanged();
	UFUNCTION()
	void HandleReturnClicked();
	UFUNCTION()
	void HandleRefreshClicked();
	UFUNCTION()
	void HandleDisconnectClicked();
	UFUNCTION()
	void HandleSteamLoginClicked();
	UFUNCTION()
	void HandleDevelopmentLoginClicked();
	UFUNCTION()
	void HandleLoginCompleted(bool bSuccess);
	UFUNCTION()
	void HandleFlowChanged(ERPGDungeonClientFlowState State, const FString& ErrorCode);
	UFUNCTION()
	void HandleSessionChanged(const FRPGDungeonSession& Session);
	UFUNCTION()
	void HandleRewardDelivered(const FRPGDungeonRewardDeliveryNotice& Notice);
	void RefreshPresentation();
	void RefreshInputNavigation();
	void PollSession();
	void BindPlayerState();
	FRPGDungeonResultActionsPresentation CurrentPresentation() const;
	void BeginAuthentication(bool bDevelopment);

	UPROPERTY(Transient)
	TObjectPtr<URPGDungeonSessionSubsystem> DungeonFlow;
	UPROPERTY(Transient)
	TObjectPtr<UHttpWebManager> WebManager;
	UPROPERTY(Transient)
	TObjectPtr<URPGSteamAuthSubsystem> Authentication;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ReturnButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> RetryButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> DisconnectButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> AbandonButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmAbandonButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelAbandonButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> AbandonmentConfirmation;
	UPROPERTY(Transient)
	TObjectPtr<UButton> LoginSteamButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> LoginDevelopmentButton;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RewardText;
	TWeakObjectPtr<ARPGPlayerState> RewardPlayerState;
	TWeakObjectPtr<ARPGPlayerController> RetryController;
	FTimerHandle PollTimer;
	double PollUntil = 0.0;
	bool bVictoryResult = false;
	bool bGameplayAbandonment = false;
	bool bDefeatStatusChecked = false;
	bool bAuthenticating = false;
	FString LoginSteamId;
	FString LoginCharacterId;
	FString LoginSessionId;
	FString AuthenticationError;
	bool bAbandonmentConfirmationOpen = false;
	int32 ConfirmAbandonmentRevision = INDEX_NONE;
	FString ConfirmAbandonmentSessionId;
	FString ConfirmAbandonmentCharacterId;
	FString ConfirmAbandonmentSteamId;
};
