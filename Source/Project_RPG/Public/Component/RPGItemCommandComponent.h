#pragma once

#include "Components/ActorComponent.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "RPGItemCommandComponent.generated.h"

class URPGInventoryProjectionComponent;
class ARPGPickUpBase;
class ARPGPlayerState;
class FRPGInventoryCommandJournal;
class URPGItemDefinition;
class IRPGItemDefinitionCatalog;
struct FRPGItemPickupGrantRequest;
namespace RPGItemCommandComponent { class FInventorySlotCommitter; }

UENUM(BlueprintType)
enum class ERPGItemCommandResultCode : uint8
{
	Succeeded,
	AlreadyApplied,
	InvalidRequest,
	Forbidden,
	Conflict,
	BackendRejected,
	ProtocolError,
	ServerStateError,
	Busy,
	Uncertain,
	NoChange
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemCommandClientResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	FName Operation;

	UPROPERTY(BlueprintReadOnly)
	ERPGItemCommandResultCode Result =
		ERPGItemCommandResultCode::ServerStateError;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGItemCommandCompleted,
	FRPGItemCommandClientResult,
	Result);

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGInventoryCommandRecovery
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) FName Operation;
	UPROPERTY(BlueprintReadOnly) bool bRetrying = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGInventoryRecoveryChanged);

/** Owner RPC facade for the asynchronous Item V2 command pipeline. */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGItemCommandComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	float GetMaximumPickupDistance() const { return MaximumPickupDistance; }
	URPGItemCommandComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(
		const EEndPlayReason::Type EndPlayReason) override;

	/** The server derives identity, quantity and destination from the world pickup. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerPickupItem(ARPGPickUpBase* Pickup);

	bool IsAuthenticatedItemOwner(const FRPGItemOwnerRef& Owner) const;
	/** Invalidates local application only; an in-flight DB commit may already exist. */
	void InvalidateForRevokedIdentity(const ARPGPlayerState& RevokedState);
	/** Terminal for this controller's identity; never means the stored command was rolled back. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	bool IsItemCommandAuthorityRevoked() const { return bAuthenticatedIdentityRevoked; }
	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	bool IsInventoryRecoveryStorageBlocked() const { return bInventoryRecoveryStorageBlocked; }
	/** Informational only: this is not an inventory-recovery request or permission to replay a pickup. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	int32 GetPendingPickupGrantCount() const { return bAuthenticatedIdentityRevoked ? 0 : PendingPickupGrantCount; }
	/** Owner-only presentation hint; the server independently checks every mutation. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	bool IsInventorySlotReservedByPickup(int32 SlotIndex) const
	{ return !bAuthenticatedIdentityRevoked && PendingPickupInventorySlots.Contains(SlotIndex); }
	bool HasInFlightCommands() const { return bInventoryRecoveryStorageBlocked || !InFlightRequestIds.IsEmpty() ||
		(Orchestrator && (Orchestrator->HasUncertainInventoryCommands() || Orchestrator->IsInventoryPersistenceBlocked())); }
	void CompletePickupGrant(const FRPGItemRepositoryCommitRequest& Request,
		ERPGItemCommandResultCode Result);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerSortInventoryTab(FGuid RequestId, EItemCategory Category);

	/** Move within this character's bag; owner and container identity are server-derived. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerMoveItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		int32 InventorySlotIndex);

	/** Only the original server-owned plan can be retried, within the same authenticated session. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerRetryUncertainInventoryCommand(FGuid RequestId);

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InventoryRecovery, Category = "RPG|Item")
	TArray<FRPGInventoryCommandRecovery> InventoryRecovery;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Item")
	FRPGInventoryRecoveryChanged OnInventoryRecoveryChanged;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerSwapInventoryItems(
		FGuid RequestId,
		FGuid SourceItemId,
		int64 ExpectedSourceRevision,
		FGuid DestinationItemId,
		int64 ExpectedDestinationRevision);

	/** Merge up to the authoritative stack limit; identity and quantity are not client authored. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerMergeInventoryItems(FGuid RequestId, FGuid SourceItemId, int64 ExpectedSourceRevision,
		FGuid DestinationItemId, int64 ExpectedDestinationRevision);

	/** Owner/container/new identity are server-derived; quantity must leave a nonempty source. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerSplitInventoryItem(FGuid RequestId, FGuid ItemId, int64 ExpectedRevision,
		int32 InventorySlotIndex, int32 Quantity);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerEquipItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		EEquipmentSlotType SlotType);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerUnequipItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		int32 InventorySlotIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerConsumeItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "RPG|Item")
	void ServerClaimMailItem(
		FGuid RequestId,
		FGuid ItemId,
		int64 ExpectedRevision,
		int32 InventorySlotIndex);

	UPROPERTY(BlueprintAssignable, Category = "RPG|Item")
	FRPGItemCommandCompleted OnItemCommandCompleted;

	/** Called by the owning controller after possession changes. */
	void HandlePawnChanged();

	bool ApplyCommitAndReconcile(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& Result,
		FString& OutError);
	bool EnqueueFirstCommitEffect(
		const FRPGItemBackendCommitResult& Result,
		FString& OutError);
	/** Internal server adapter: freeze the original effect/receipt/avatar before HTTP. */
	bool PrepareConsumeEffect(const FRPGItemRepositoryCommitRequest& Request, FString& OutError);
	void DiscardConsumeEffect(const FGuid& RequestId);

private:
	friend class RPGItemCommandComponent::FInventorySlotCommitter;
	TUniquePtr<IRPGItemAsyncCommitter> MakeSlotAwareCommitter(TUniquePtr<IRPGItemAsyncCommitter> Inner);
	bool TrackInventorySlotRequest(const FRPGItemRepositoryCommitRequest& Request);
	bool IsInventoryLocationReserved(const FRPGItemLocation& Location) const;
	friend class URPGPickupGrantComponent;
	bool TrackPickupGrant(const FRPGItemRepositoryCommitRequest& Request);
	bool TryPlanPickupGrant(const FRPGItemPickupGrantRequest& Request, const URPGItemDefinition& Definition,
		const IRPGItemRecordSource& Records, const IRPGItemDefinitionCatalog& Definitions,
		FRPGItemRepositoryCommitRequest& OutCommit) const;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct FRPGPickupSlotReservationTestAccess;
#endif
	void RefreshPickupGrantCount();
	friend struct FRPGDungeonAuthorityRevocationTestAccess;
	friend struct FRPGInventoryRecoveryRevocationTestAccess;
	friend struct FRPGConsumeRecoveryTestAccess;
	bool PrepareConsumeEffectFromDefinition(const FRPGItemRepositoryCommitRequest& Request,
		const URPGItemDefinition* Definition, FString& OutError);
	bool EnsureOrchestrator();
	void BlockInventoryRecoveryStorage();
	bool TryGetAuthenticatedOwner(FRPGItemOwnerRef& OutOwner) const;
	bool BeginRequest(const FGuid& RequestId, FName Operation, bool bRecoveryRetry = false);
	UFUNCTION()
	void OnRep_InventoryRecovery();
	void SetInventoryRecovery(FGuid RequestId, FName Operation, bool bUncertain, bool bRetrying = false);
	void HandleOutcome(
		FGuid RequestId,
		FName Operation,
		FRPGItemAsyncCommandOutcome Outcome);
	void HandleAuthoritativeRecordsChanged();
	bool ReconcileEquipmentFromProjection(FString* OutError = nullptr);

	UFUNCTION(Client, Reliable)
	void ClientNotifyItemCommand(FRPGItemCommandClientResult Result);

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Item",
		meta = (ClampMin = "1", ClampMax = "32"))
	int32 MaximumInFlightCommands = 8;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Item", meta = (ClampMin = "1.0"))
	float MaximumPickupDistance = 400.f;

	bool bEndingPlay = false;
	UPROPERTY(ReplicatedUsing = OnRep_InventoryRecovery)
	bool bAuthenticatedIdentityRevoked = false;
	UPROPERTY(ReplicatedUsing = OnRep_InventoryRecovery)
	bool bInventoryRecoveryStorageBlocked = false;
	UPROPERTY(ReplicatedUsing = OnRep_InventoryRecovery)
	int32 PendingPickupGrantCount = 0;
	UPROPERTY(ReplicatedUsing = OnRep_InventoryRecovery)
	TArray<int32> PendingPickupInventorySlots;
	double NextInventoryRecoveryRetryTime = 0.0;

	TSet<FGuid> InFlightRequestIds;
	TMap<FGuid, FRPGItemLocation> PendingPickupLocations;
	TMap<FGuid, FRPGItemRepositoryCommitRequest> PendingInventorySlotRequests;
	TSet<FGuid> AppliedFirstCommitEffects;
	TMap<FGuid, FRPGItemBackendCommitResult> PendingFirstCommitEffects;
	struct FConsumeEffectTarget;
	TMap<FGuid, TSharedPtr<FConsumeEffectTarget>> ConsumeEffectTargets;
	FDelegateHandle ProjectionChangedHandle;
	TSharedPtr<FRPGInventoryCommandJournal> InventoryJournal;

	TUniquePtr<IRPGItemAsyncCommitter> CommitterAdapter;
	TUniquePtr<IRPGItemCommitSink> CommitSinkAdapter;
	TUniquePtr<IRPGItemFirstCommitSink> FirstCommitSinkAdapter;
	TSharedPtr<
		FRPGItemAsyncCommandOrchestrator,
		ESPMode::ThreadSafe> Orchestrator;
};
