#pragma once

#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "GameplayEffect.h"
#include "Http.h"
#include "Online/RPGDungeonAbandonment.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGDungeonFlowE2EProbe.generated.h"

struct FRPGBackendCharacter;
struct FRPGInventoryStackMergeE2E;
struct FRPGInventoryStackSplitE2E;
struct FRPGConsumeRecoveryServerProbe;
class APlayerController;
class APawn;
class ARPGPlayerState;
class ARPGBossCharacter;
class UNetConnection;
class UEnhancedInputLocalPlayerSubsystem;
class UInputAction;

/** Survives both real client travels. Never seeds private flow state or an access token. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonFlowE2EClient final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	UFUNCTION()
	void HandleLogin(bool bSuccess);
	UFUNCTION()
	void HandleCharacterCreated(const FRPGBackendCharacter& Character, bool bSuccess);
	UFUNCTION()
	void HandleLobbyTravel(const FString& Destination);
	UFUNCTION()
	void HandleInitialAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt, bool bSuccess, const FString& ErrorCode);
	UFUNCTION()
	void HandleResultAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt, bool bSuccess, const FString& ErrorCode);
	UFUNCTION()
	void HandleGameplayAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt, bool bSuccess, const FString& ErrorCode);
	UFUNCTION()
	void HandleLobbyLogout(bool bSuccess);
	bool TickProbe(float DeltaTime);
	bool TickDepartureRecoveryProbe();
	bool TickDepartureRestartProbe();
	bool TickLobbyLogoutProbe(class UWorld* World, APlayerController* PC);
	bool TickLobbyConfigurationProbe(class UWorld* World, APlayerController* PC);
	void TickNaturalPlayerAttack(class UWorld* World, APlayerController* PC);
	void StopNaturalPlayerAttack();
	bool TickPartyHudInput(class UWorld* World, APlayerController* PC);
	void StopPartyHudInput();
	bool TickAbandonmentProbe(class UWorld* World, APlayerController* PC);
	bool TickResultAbandonmentProbe(class UWorld* World, APlayerController* PC);
	bool TickGameplayAbandonmentProbe(class UWorld* World, APlayerController* PC);
	bool ClickFlowButton(class UButton* Button);
	bool VerifyFlowConfirmationNavigation(class UButton* Open, class UButton* Confirm, class UButton* Cancel);
	bool Fail(const TCHAR* Reason);
	FTSTicker::FDelegateHandle TickerHandle;
	TSharedPtr<FRPGInventoryStackMergeE2E> InventoryStackMergeProbe;
	TSharedPtr<FRPGInventoryStackSplitE2E> InventoryStackSplitProbe;
	FString CharacterId;
	FString CharacterName;
	FString SteamId;
	FString SessionId;
	FString SessionFile;
	FString ServerReadyFile;
	FString NaturalAttackGateFile;
	FString PartyHudInputGate;
	bool bPartyHudInput = false;
	int32 PartyHudInputPhase = 0;
	int32 PartyHudInputPass = 0;
	double PartyHudInputAt = 0.0;
	FVector PartyHudInputStart = FVector::ZeroVector;
	FVector2D PartyHudInputPoint = FVector2D::ZeroVector;
	FName PartyHudMoveKeyName;
	TWeakObjectPtr<class URPGDungeonPartyCombatWidget> PartyHudInputWidget;
	TWeakObjectPtr<APawn> PartyHudInputPawn;
	TWeakPtr<class SWidget> PartyHudInputFocus;
	bool bPartyHudMoveDown = false;
	bool bPartyHudAttackDown = false;
	bool bPartyHudSawAttack = false;
	bool bPartyHudCursor = false;
	FString AuthExpiredFile;
	FString DungeonAuthExpiredFile;
	FString AbandonmentStartFile;
	FString AbandonmentRetryFile;
	FString AbandonmentRenewedRetryFile;
	FString InitialAbandonmentGateFile;
	bool bGameplayAbandonment = false;
	bool bGameplayAbandonmentResponseLoss = false;
	bool bGameplayAbandonmentReceipt = false;
	FString GameplayConsentGateFile;
	FString GameplayConfirmGateFile;
	int32 GameplayConsentPhase = 0;
	int32 GameplayPlayerId = INDEX_NONE;
	TWeakObjectPtr<APlayerController> GameplayOriginalController;
	TWeakObjectPtr<APawn> GameplayOriginalPawn;
	TWeakObjectPtr<ARPGPlayerState> GameplayOriginalState;
	TWeakObjectPtr<UNetConnection> GameplayOriginalConnection;
	bool bGameplayPreviousMoveIgnored = false;
	bool bGameplayPreviousLookIgnored = false;
	bool bGameplayPreviousCursor = false;
	FString ResultAbandonCharacter;
	FString ResultRetryGateFile;
	bool bResultAbandonment = false;
	bool bResultAbandonmentResponseLoss = false;
	bool bResultAbandonmentReceipt = false;
	bool bSlatePointerInput = false;
	bool bConfirmationNavigation = false;
	int32 ResultAbandonmentPhase = 0;
	int32 ResultDefeatRevision = INDEX_NONE;
	TWeakObjectPtr<APawn> ResultOriginalPawn;
	TWeakObjectPtr<ARPGBossCharacter> ResultOriginalBoss;
	int32 Stage = 0;
	int32 DeparturePhase = 0;
	FString FirstDepartureRequestId;
	int64 DepartureRestartRevision = 0;
	bool bLobbyUI = false;
	bool bLobbyCreationOnly = false;
	bool bLobbyConfigurationOnly = false;
	FString ConfigurationOtherCharacterId;
	bool bLobbyDifficultyOnly = false;
	FString LobbyDifficulty = TEXT("Hard");
	bool bVerifyLobbyLogout = false;
	FString LogoutNextSteamId;
	FString LogoutNextCharacterId;
	FHttpRequestPtr LogoutCredentialProbe;
	int32 LogoutCompletionCount = 0;
	bool bRetiredLogoutCredentialRejected = false;
	bool bAbandonmentUI = false;
	bool bAbandonmentReauthenticate = false;
	bool bVerifyAbandonedReturn = false;
	bool bVerifyAbandonedReceipt = false;
	bool bVerifyDungeonClientInitialAbandonment = false;
	bool bInitialAbandonmentSubmitted = false;
	bool bInitialAbandonmentReceiptObserved = false;
	bool bLobbyLeader = false;
	bool bCreateCharacter = false;
	bool bCharacterCreateStarted = false;
	bool bCharacterCreated = false;
	bool bCharacterSelected = false;
	bool bSessionActionStarted = false;
	bool bLoginStarted = false;
	bool bReauthenticate = false;
	bool bReauthenticated = false;
	bool bReauthenticateAfterAdmission = false;
	bool bExpiredInDungeon = false;
	bool bResultReauthenticated = false;
	bool bResultReauthenticationVerified = false;
	bool bSawReturning = false;
	bool bNaturalPlayerAttack = false;
	bool bNaturalAttackSawActivation = false;
	int32 NaturalAttackPhase = 0;
	double NaturalAttackPhaseAt = 0.0;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> NaturalAttackInput;
	TWeakObjectPtr<const UInputAction> NaturalAttackMoveAction;
	TWeakObjectPtr<const UInputAction> NaturalAttackAction;
	double Deadline = 0.0;
};

/** Opt-in server hit fixture; uses the production damage execution and Dead -> Victory path. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonFlowE2EDamageEffect final : public UGameplayEffect
{
	GENERATED_BODY()
public:
	URPGDungeonFlowE2EDamageEffect();
};

UCLASS(Transient, NotBlueprintable)
class URPGDungeonFlowE2EServer final : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGDungeonFlowE2EServer();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
private:
	TSharedPtr<FRPGConsumeRecoveryServerProbe> ConsumeRecoveryProbe;
	void Fail(const TCHAR* Reason);
	void ObservePendingAdmissionAbandonment();
	bool ObserveAbandonmentParty();
	void ObservePartyCombatVitals();
	bool ObserveResultAbandonmentRetry();
	UFUNCTION()
	void HandleMemberRevoked(const FString& CharacterId);
	struct FPartyPeer
	{
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<ARPGPlayerState> State;
		TWeakObjectPtr<UNetConnection> Connection;
		FString CharacterId;
		FString SteamId;
		int64 ConnectionGeneration = 0;
	};
	TArray<FPartyPeer> InitialParty;
	FPartyPeer ReconnectedPeer;
	TWeakObjectPtr<ARPGBossCharacter> InitialBoss;
	FString AbandonCharacter;
	bool bResultAbandonment = false;
	int32 ResultAbandonmentPhase = 0;
	int32 ResultInitialRevision = INDEX_NONE;
	FString ResultSettleGateFile;
	TWeakObjectPtr<APawn> ResultSurvivorPawn;
	bool bAbandonmentSettlement = false;
	bool bGameplayAbandonmentSettlement = false;
	bool bUnexpectedDisconnectSettlement = false;
	bool bReconnectWithinGrace = false;
	bool bPartyCombatVitals = false;
	int32 PartyVitalsPhase = 0;
	float PartyVitalsTargetBefore = 0.f;
	float PartyVitalsSurvivorBefore = 0.f;
	bool bReconnectAdmissionLoss = false;
	TWeakObjectPtr<APlayerController> PendingReconnectController;
	int64 PendingReconnectGeneration = 0;
	bool bPendingReconnectCleaned = false;
	bool bUnexpectedLossObserved = false;
	bool bExpiryObserved = false;
	bool bNaturalPlayerAttack = false;
	bool bLiveBossAI = false;
	bool bLiveBossAIStarted = false;
	bool bLiveBossAITelegraph = false;
	bool bLiveBossAITelegraphFinished = false;
	bool bLiveBossAIDamage = false;
	int32 LiveBossAITelegraphPhase = INDEX_NONE;
	bool bNaturalSawDamage = false;
	float NaturalBossHealthBefore = 0.f;
	TMap<FString, float> LiveBossAIPlayerHealthBefore;
	bool bRevocationObserved = false;
	bool bRemainingPartyReady = false;
	int32 InitialEncounterId = 0;
	double RevocationTime = 0.0;
	FString GatePath;
	FString NaturalAttackGateFile;
	int32 ExpectedPlayers = 0;
	int32 Stage = 0;
	double Deadline = 0.0;
};
