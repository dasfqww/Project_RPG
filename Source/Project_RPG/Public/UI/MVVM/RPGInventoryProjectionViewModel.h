#pragma once

#include "Component/RPGItemCommandComponent.h"
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Type/RPGEnumTypes.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "RPGInventoryProjectionViewModel.generated.h"

class APlayerController;
class ARPGPlayerState;
class URPGInventoryProjectionComponent;
class IRPGItemActionPolicyCatalog;
class UTexture2D;
struct FRPGItemDefinitionSnapshot;

enum class ERPGInventoryOccupiedDropAction : uint8 { Rejected, Swap, Merge };

/** Presentation only; Idle is not a promise that a particular item action is allowed. */
UENUM(BlueprintType)
enum class ERPGItemSlotPresentationState : uint8
{
	Idle,
	Locked,
	Pending,
	RecoveryRequired,
	Unavailable
};

/** Advisory bag counts from the last owner snapshot, not permission to grant an item. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGInventoryCapacityPresentation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	int32 TotalSlots = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	int32 OccupiedSlots = 0;
	/** Reservations on empty slots only; occupied reservations are not counted twice. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	int32 ReservedEmptySlots = 0;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	int32 FreeSlots = 0;
	/** False means the counts are unknown, not that the bag is empty or full. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	bool bHasSnapshot = false;
	/** Even when space exists, recovery or unavailable command access can block changes. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	bool bItemChangesBlocked = true;
};

/** Local claim availability only; the server still validates every submitted reward. */
UENUM(BlueprintType)
enum class ERPGMailClaimState : uint8
{
	Available,
	Pending,
	Locked,
	InventoryFull,
	InventoryReserved,
	RecoveryRequired,
	RecoveryStorageBlocked,
	Unavailable
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGMailClaimPresentation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	ERPGMailClaimState State = ERPGMailClaimState::Unavailable;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	FText ActionLabel;
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item|ViewModel")
	FText Reason;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGInventoryProjectionCommandCompleted,
	FRPGItemCommandClientResult,
	Result);

/**
 * UI-only adapter for Item V2's owner projection and command facade.
 *
 * This ViewModel deliberately exposes projected entries rather than legacy
 * URPGItemBase objects. Commands carry only an item identity and the current
 * projected revision; the server still owns all authorization and mutation.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGInventoryProjectionViewModel final
	: public URPGViewModelBase
{
	GENERATED_BODY()

public:
	/** Owner-visible text only; never an item command or persisted state. */
	struct FItemPresentation
	{
		FText DisplayName;
		FText Description;
		FText InstanceDetails;
		TSoftObjectPtr<UTexture2D> Icon;
		int32 Quantity = 0;

		FText GetDetailsText() const;
	};

	virtual void BeginDestroy() override;

	/** Bind this ViewModel to the local player controller's Item V2 components. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	void InitializeFromPlayerController(APlayerController* PlayerController);

	/** Read-only binding check; opening another window must not reset in-flight requests. */
	bool IsBoundToPlayerController(const APlayerController* PlayerController) const;

	/** Retry a failed owner projection load without exposing backend credentials to UI. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestRetryLoad();

	/** Retry one server-retained bag command without accepting a new ID, revision or destination. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestRetryInventoryCommand();
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bHasInventoryRecovery = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bCanRetryInventoryRecovery = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bInventoryRecoveryInFlight = false;
	/** This identity can no longer use items. This is not a successful recovery receipt. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bInventoryAuthorityRevoked = false;
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bInventoryRecoveryStorageBlocked = false;

	/** Local window reservations plus the server-reported number of pending world pickups. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	int32 PendingItemCommandCount = 0;

	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	ERPGItemSlotPresentationState GetItemSlotPresentationState(FGuid ItemId,
		ERPGItemContainerType ContainerType, int32 SlotIndex = -1) const;

	/** A stack occupies one slot; pending destinations do not become confirmed items. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	FRPGInventoryCapacityPresentation GetInventoryCapacity() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	FRPGInventoryCapacityPresentation GetInventoryCapacityForCategory(EItemCategory Category) const;
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	bool CanSortInventoryTab(EItemCategory Category) const;
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestSortInventoryTab(EItemCategory Category, FGuid& OutRequestId);

	/** Capture a live revision at drag start; stale drags must not submit newer state. */
	bool TryGetInventoryDragEntry(FGuid ItemId, FRPGInventoryProjectionEntry& OutEntry) const;
	bool CanMoveItemToSlot(FGuid ItemId, int64 ExpectedRevision, int32 InventorySlotIndex) const;
	bool RequestMoveItem(FGuid ItemId, int64 ExpectedRevision,
		int32 InventorySlotIndex, FGuid& OutRequestId);
	bool CanSwapItems(FGuid SourceItemId, int64 ExpectedSourceRevision,
		FGuid DestinationItemId, int64 ExpectedDestinationRevision) const;
	bool RequestSwapItems(FGuid SourceItemId, int64 ExpectedSourceRevision,
		FGuid DestinationItemId, int64 ExpectedDestinationRevision, FGuid& OutRequestId);
	/** Same stackable type merges; a full/incompatible stack never silently swaps. */
	ERPGInventoryOccupiedDropAction GetOccupiedDropAction(FGuid SourceItemId, int64 ExpectedSourceRevision,
		FGuid DestinationItemId, int64 ExpectedDestinationRevision) const;
	static ERPGInventoryOccupiedDropAction BuildOccupiedDropAction(
		const FRPGInventoryProjectionEntry& Source, const FRPGInventoryProjectionEntry& Destination,
		const FRPGItemDefinitionSnapshot* Definition);
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestMergeItems(FGuid SourceItemId, int64 ExpectedSourceRevision,
		FGuid DestinationItemId, int64 ExpectedDestinationRevision, FGuid& OutRequestId);
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	bool CanSplitItem(FGuid ItemId, int64 ExpectedRevision, int32 InventorySlotIndex, int32 Quantity) const;
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestSplitItem(FGuid ItemId, int64 ExpectedRevision, int32 InventorySlotIndex,
		int32 Quantity, FGuid& OutRequestId);

	/** Resolve local authoring text without loading assets or exposing legacy objects. */
	FItemPresentation GetItemPresentation(FGuid ItemId) const;
	FItemPresentation GetItemPresentationUsingCatalog(
		FGuid ItemId, const class IRPGItemDefinitionViewCatalog* Catalog) const;

	/** Shared read-only detail text for authored WBP views and native item cells. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	FText GetItemDetailsText(FGuid ItemId) const;

	/** Definition icon reference only; loading belongs to the view, never to commands. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	TSoftObjectPtr<UTexture2D> GetItemIcon(FGuid ItemId) const;

	/** Recovery, local preflight feedback, then the latest server acknowledgement. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	FText GetCommandFeedbackText() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestConsume(FGuid ItemId, FGuid& OutRequestId);

	/** Use a consumable or equip into the first free compatible equipment slot. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestDefaultAction(FGuid ItemId, FGuid& OutRequestId);

	/** Native policy boundary; the catalog is only borrowed for this call. */
	bool RequestDefaultActionUsingPolicies(FGuid ItemId,
		const IRPGItemActionPolicyCatalog& Policies, FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestUnequipToFirstFreeSlot(FGuid ItemId, FGuid& OutRequestId);

	/** Submit an inventory item ID to an item shortcut; the server validates its policy. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool AssignItemToQuickSlot(FGuid ItemId, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestEquip(
		FGuid ItemId,
		EEquipmentSlotType SlotType,
		FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestUnequip(
		FGuid ItemId,
		int32 InventorySlotIndex,
		FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestClaimMail(FGuid ItemId, int32 InventorySlotIndex, FGuid& OutRequestId);

	/** Select a free slot from the read model; authority checks again at commit. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestClaimMailToFirstFreeSlot(FGuid ItemId, FGuid& OutRequestId);

	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	bool IsMailClaimPending(FGuid ItemId) const;

	/** No RPC, local-clock expiry decision, reservation release or item mutation. */
	UFUNCTION(BlueprintPure, Category = "RPG|Item|ViewModel")
	FRPGMailClaimPresentation GetMailClaimPresentation(FGuid ItemId) const;

	/** Projection state for loading and empty-state UI. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	ERPGInventoryProjectionLoadState LoadState =
		ERPGInventoryProjectionLoadState::Uninitialized;

	/** Active owner records in the Inventory container, ordered by slot. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	TArray<FRPGInventoryProjectionEntry> InventoryItems;

	/** Active owner records in the Equipment container, ordered by slot. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	TArray<FRPGInventoryProjectionEntry> EquipmentItems;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	TArray<FRPGInventoryProjectionEntry> MailItems;

	/** Owner-only settlement feedback, also restored when opening the UI later. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	FText RewardDeliveryMessage;

	/** The most recent owner-only command outcome for UI feedback. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	FRPGItemCommandClientResult LastCommandResult;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bHasLastCommandResult = false;

	/** A local action was not submitted. Never a server receipt or a reserved request. */
	UPROPERTY(BlueprintReadOnly, Transient, FieldNotify, Category = "RPG|Item|ViewModel")
	FText LocalActionFeedback;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Item|ViewModel")
	FRPGInventoryProjectionCommandCompleted OnCommandCompleted;

private:
	friend struct FRPGInventoryTabsTestAccess;
	void SetLocalActionFeedback(const FText& Feedback);
	bool RejectLocalAction(const FText& Reason);
	ERPGMailClaimState ResolveMailClaimState(FGuid ItemId) const;
	friend class FRPGRewardMailReservationLifecycleTest;
	friend class FRPGMailClaimPresentationTest;
	friend class FRPGItemPendingActionPresentationTest;
	friend class FRPGItemLocalActionFeedbackTest;
	friend class FRPGItemStackMergeReservationTest;
	friend class FRPGItemStackSplitReservationTest;
	friend class FRPGInventoryCommandRecoveryUITest;
	friend class FRPGInventoryRecoveryRevokedUITest;

	struct FPendingInventorySort
	{
		TMap<FGuid, int64> Revisions;
		bool bCommitted = false;
	};
	TMap<FGuid, FPendingInventorySort> PendingInventorySorts;
	void ReconcilePendingInventorySorts();

	struct FPendingInventoryMove
	{
		FGuid ItemId;
		int64 ExpectedRevision = 0;
		int32 DestinationSlot = INDEX_NONE;
		FGuid SwapItemId;
		int64 ExpectedSwapRevision = 0;
		int32 SourceSlot = INDEX_NONE;
		bool bWaitForCreatedItem = false;
	};

	struct FPendingItemAction
	{
		FGuid ItemId;
		int64 ExpectedRevision = 0;
		ERPGItemContainerType SourceContainer = ERPGItemContainerType::None;
		int32 SourceSlot = INDEX_NONE;
		ERPGItemContainerType DestinationContainer = ERPGItemContainerType::None;
		int32 DestinationSlot = INDEX_NONE;
	};

	UFUNCTION()
	void HandleProjectionChanged();

	UFUNCTION()
	void HandleLoadStateChanged(
		ERPGInventoryProjectionLoadState NewLoadState);

	UFUNCTION()
	void HandleCommandCompleted(FRPGItemCommandClientResult Result);
	UFUNCTION()
	void HandleInventoryRecoveryChanged();
	bool TickInventoryRecoveryCooldown(float DeltaSeconds);
	bool IsItemAuthorityRevoked() const;

	UFUNCTION()
	void HandleDungeonRewardDelivered(const FRPGDungeonRewardDeliveryNotice& Notice);

	bool TryGetEntry(
		const FGuid& ItemId,
		ERPGItemContainerType ExpectedContainer,
		FRPGInventoryProjectionEntry& OutEntry) const;
	bool IsInventorySlotUnavailable(int32 SlotIndex) const;
	bool IsInventorySlotReserved(int32 SlotIndex) const;
	bool IsEquipmentSlotReserved(int32 SlotIndex) const;
	bool IsEquipmentSlotUnavailable(int32 SlotIndex) const;
	bool IsItemActionPending(FGuid ItemId) const;
	void RefreshPendingItemCommandCount();
	bool IsMailClaimSettledInProjection(const FGuid& ItemId) const;
	void ReconcilePendingMailClaims();
	void ReconcilePendingInventoryMoves();
	void ReconcilePendingItemActions();
	void RefreshProjection();
	void UnbindComponents();

	TWeakObjectPtr<URPGInventoryProjectionComponent> ProjectionComponent;
	TWeakObjectPtr<APlayerController> BoundPlayerController;
	TWeakObjectPtr<URPGItemCommandComponent> CommandComponent;
	TWeakObjectPtr<ARPGPlayerState> RewardPlayerState;
	TMap<FGuid, FPendingInventoryMove> PendingInventoryMovesByRequest;
	TMap<FGuid, FPendingItemAction> PendingItemActionsByRequest;
	TMap<FGuid, FGuid> PendingMailClaimsByRequest;
	TSet<FGuid> PendingMailItemIds;
	TMap<FGuid, int32> PendingMailSlotsByItem;
	double NextInventoryRecoveryRequestTime = 0.0;
	FGuid LocalInventoryRecoveryRequest;
	FTSTicker::FDelegateHandle InventoryRecoveryCooldownTicker;
};
