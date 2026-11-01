#pragma once

#include "Controller/RPGPlayerController.h"
#include "GameMode/RPGGameModeBase.h"
#include "Component/RPGItemCommandComponent.h"
#include "GameFramework/Pawn.h"
#include "RPGDungeonRewardE2EGameMode.generated.h"

/** Headless test controller; uses the production identity, wallet and item components. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonRewardE2EController : public ARPGPlayerController
{
	GENERATED_BODY()
public:
	virtual void Tick(float DeltaSeconds) override;
	bool HasConfirmedInitialRewardState() const { return bInitialRewardStateConfirmed; }
	UFUNCTION(Server, Reliable)
	void ServerConfirmInitialRewardState();
protected:
	virtual void SetupInputComponent() override;
private:
	bool bProbeFinished = false;
	bool bInitialRewardStateConfirmed = false;
	bool bAwaitingLiveRewardRefresh = false;
	UPROPERTY(Transient)
	TObjectPtr<class URPGInventoryProjectionWidget> LiveRewardWidget;
	int32 ClaimStep = 0;
	int32 ConcurrentClaimSuccessCount = 0;
	bool bAwaitingClaim = false;
	TSet<FGuid> ConcurrentCompletedClaimIds;
	FGuid PotionId, HelmId, OriginalClaimId, PendingClaimId;
	ERPGItemCommandResultCode ExpectedClaimResult = ERPGItemCommandResultCode::Succeeded;
	UPROPERTY(Transient)
	TObjectPtr<class URPGInventoryProjectionViewModel> ClaimViewModel;
	UFUNCTION()
	void HandleClaimResult(FRPGItemCommandClientResult Result);
	void SendClaim(FGuid RequestId, FGuid ItemId, int64 Revision, int32 Slot,
		ERPGItemCommandResultCode Expected);
	bool ClickMailClaimButton(const FGuid& ItemId);
	bool ValidateRestoredRewards(bool bClaimed) const;
	void TickInventoryOverflow();
	bool ValidateOverflowRewards(bool bClaimed) const;
	bool ClickFullInventoryClaimButton(const FGuid& ItemId);
	void HandleOverflowCommandResult(FRPGItemCommandClientResult Result);
};

/** Minimal possessed test pawn retains the production security-action gate. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonRewardE2EPawn : public APawn
{
	GENERATED_BODY()
public:
	ARPGDungeonRewardE2EPawn();
};

/** Explicitly opted-in, non-shipping fixture for the dedicated-server settlement path. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonRewardE2EGameMode : public ARPGGameModeBase
{
	GENERATED_BODY()
public:
	ARPGDungeonRewardE2EGameMode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
private:
	UFUNCTION()
	void HandleProbeSession(const FRPGDungeonSession& Session, bool bSuccess);
	bool bProbeEnabled = false;
	bool bSessionStarted = false;
	bool bServerTickReadyReported = false;
	bool bProbeFinished = false;
	int32 ExpectedPlayers = 1;
	TMap<FGuid, TPair<FGuid, FGuid>> ExpectedRewardItems;
};
