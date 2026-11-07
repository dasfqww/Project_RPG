#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Policy/RPGItemStackPolicy.h"
#include "Item/Transaction/RPGItemActionCommandPlanner.h"
#include "Manager/DataManager.h"
#include "Player/RPGPlayerState.h"
#include "UI/Inventory/Projection/RPGItemCommandFeedbackFormatter.h"
#include "UI/Inventory/Projection/RPGItemTooltipFormatter.h"
#include "HAL/PlatformTime.h"
#include "Containers/Ticker.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGInventoryProjectionViewModel)

FText URPGInventoryProjectionViewModel::FItemPresentation::GetDetailsText() const
{
	return FRPGItemTooltipFormatter::JoinDetails(DisplayName, Description, InstanceDetails);
}

FText URPGInventoryProjectionViewModel::GetCommandFeedbackText() const
{
	if (IsItemAuthorityRevoked()) return FText::GetEmpty();
	const auto* Commands = CommandComponent.Get();
	if (bInventoryRecoveryStorageBlocked || (Commands && Commands->IsInventoryRecoveryStorageBlocked()))
		return NSLOCTEXT("RPGItemFeedback", "RecoveryStorageBlocked",
			"Item changes are blocked because recovery storage is unavailable. Stored commands have not been discarded.");
	if (bHasInventoryRecovery || (Commands && !Commands->InventoryRecovery.IsEmpty()))
	{
		if (bInventoryRecoveryInFlight || (Commands && Commands->InventoryRecovery.ContainsByPredicate(
			[](const FRPGInventoryCommandRecovery& Entry) { return Entry.bRetrying; })))
			return NSLOCTEXT("RPGItemFeedback", "RecoveryInFlight",
				"Checking the original item command. Do not repeat the original action.");
		return NSLOCTEXT("RPGItemFeedback", "RecoveryRequired",
			"An earlier item save is still unconfirmed. Use Recover pending item command when available; do not repeat the original action.");
	}
	if (!LocalActionFeedback.IsEmpty()) return LocalActionFeedback;
	if (Commands && Commands->GetPendingPickupGrantCount() > 0)
		return NSLOCTEXT("RPGItemFeedback", "PickupPending",
			"Confirming an item pickup. Do not repeat the same pickup.");
	return bHasLastCommandResult
		? FRPGItemCommandFeedbackFormatter::Build(LastCommandResult) : FText::GetEmpty();
}

void URPGInventoryProjectionViewModel::SetLocalActionFeedback(const FText& Feedback)
{
	if (LocalActionFeedback.EqualTo(Feedback)) return;
	LocalActionFeedback = Feedback;
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LocalActionFeedback);
}

bool URPGInventoryProjectionViewModel::RejectLocalAction(const FText& Reason)
{
	SetLocalActionFeedback(Reason);
	return false;
}

void URPGInventoryProjectionViewModel::BeginDestroy()
{
	UnbindComponents();
	Super::BeginDestroy();
}

void URPGInventoryProjectionViewModel::InitializeFromPlayerController(
	APlayerController* PlayerController)
{
	UnbindComponents();
	BoundPlayerController = PlayerController;
	SetLocalActionFeedback(FText::GetEmpty());
	bInventoryAuthorityRevoked = false; // The new binding reads its own controller's terminal state below.
	PendingInventoryMovesByRequest.Reset();
	PendingInventorySorts.Reset();
	PendingItemActionsByRequest.Reset();
	PendingMailClaimsByRequest.Reset();
	PendingMailItemIds.Reset();
	PendingMailSlotsByItem.Reset();
	RefreshPendingItemCommandCount();
	RewardDeliveryMessage = FText::GetEmpty();
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RewardDeliveryMessage);

	LastCommandResult = FRPGItemCommandClientResult();
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LastCommandResult);
	if (bHasLastCommandResult)
	{
		bHasLastCommandResult = false;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasLastCommandResult);
	}

	URPGInventoryProjectionComponent* Projection = PlayerController
		? PlayerController->FindComponentByClass<
			URPGInventoryProjectionComponent>()
		: nullptr;
	URPGItemCommandComponent* Commands = PlayerController
		? PlayerController->FindComponentByClass<URPGItemCommandComponent>()
		: nullptr;
	ProjectionComponent = Projection;
	CommandComponent = Commands;
	RewardPlayerState = PlayerController ? PlayerController->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (ARPGPlayerState* State = RewardPlayerState.Get())
	{
		State->OnDungeonRewardDelivered.AddUniqueDynamic(this, &ThisClass::HandleDungeonRewardDelivered);
		HandleDungeonRewardDelivered(State->GetRewardDeliveryNotice());
	}

	if (Projection)
	{
		Projection->OnProjectionChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleProjectionChanged);
		Projection->OnLoadStateChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleLoadStateChanged);
	}
	if (Commands)
	{
		Commands->OnInventoryRecoveryChanged.AddUniqueDynamic(this, &ThisClass::HandleInventoryRecoveryChanged);
		Commands->OnItemCommandCompleted.AddUniqueDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
	}

	HandleInventoryRecoveryChanged();
	HandleLoadStateChanged(
		Projection
			? Projection->GetLoadState()
			: ERPGInventoryProjectionLoadState::Uninitialized);
}

bool URPGInventoryProjectionViewModel::IsBoundToPlayerController(const APlayerController* PlayerController) const
{
	return PlayerController && BoundPlayerController.Get() == PlayerController &&
		ProjectionComponent.Get() == PlayerController->FindComponentByClass<URPGInventoryProjectionComponent>() &&
		CommandComponent.Get() == PlayerController->FindComponentByClass<URPGItemCommandComponent>() &&
		RewardPlayerState.Get() == PlayerController->GetPlayerState<ARPGPlayerState>();
}

bool URPGInventoryProjectionViewModel::RequestRetryLoad()
{
	URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	const APlayerController* Controller = Projection
		? Cast<APlayerController>(Projection->GetOwner()) : nullptr;
	if (IsItemAuthorityRevoked() || LoadState != ERPGInventoryProjectionLoadState::Failed
		|| !Controller || !Controller->IsLocalController()) return false;
	Projection->ServerRetryLoadAuthenticatedCharacterItems();
	return true;
}

bool URPGInventoryProjectionViewModel::IsItemAuthorityRevoked() const
{
	const auto* Commands = CommandComponent.Get();
	return bInventoryAuthorityRevoked || (Commands && Commands->IsItemCommandAuthorityRevoked());
}

void URPGInventoryProjectionViewModel::HandleInventoryRecoveryChanged()
{
	const auto* Commands = CommandComponent.Get();
	const bool bNewRevocation = !bInventoryAuthorityRevoked && IsItemAuthorityRevoked();
	bInventoryRecoveryStorageBlocked = Commands && Commands->IsInventoryRecoveryStorageBlocked();
	bInventoryAuthorityRevoked = IsItemAuthorityRevoked(); // One-way until bound to a different controller.
	bHasInventoryRecovery = !bInventoryAuthorityRevoked && (bInventoryRecoveryStorageBlocked || (Commands && !Commands->InventoryRecovery.IsEmpty()));
	if (!bHasInventoryRecovery) LocalInventoryRecoveryRequest.Invalidate();
	bInventoryRecoveryInFlight = !bInventoryAuthorityRevoked && (LocalInventoryRecoveryRequest.IsValid() || (Commands && Commands->InventoryRecovery.ContainsByPredicate(
		[](const FRPGInventoryCommandRecovery& Entry) { return Entry.bRetrying; })));
	bCanRetryInventoryRecovery = !bInventoryAuthorityRevoked && !bInventoryRecoveryStorageBlocked && !bInventoryRecoveryInFlight && FPlatformTime::Seconds() >= NextInventoryRecoveryRequestTime &&
		Commands && Commands->InventoryRecovery.ContainsByPredicate(
		[](const FRPGInventoryCommandRecovery& Entry) { return !Entry.bRetrying; });
	if (bNewRevocation)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(InventoryRecoveryCooldownTicker);
		InventoryRecoveryCooldownTicker.Reset();
		NextInventoryRecoveryRequestTime = 0.0;
		// Retire only local UI handles; this does not cancel or undo a database commit.
		PendingInventoryMovesByRequest.Reset();
		PendingInventorySorts.Reset();
		PendingItemActionsByRequest.Reset();
		PendingMailClaimsByRequest.Reset();
		PendingMailItemIds.Reset();
		PendingMailSlotsByItem.Reset();
		LastCommandResult = FRPGItemCommandClientResult();
		bHasLastCommandResult = false;
		LocalActionFeedback = FText::GetEmpty();
		RewardDeliveryMessage = FText::GetEmpty();
		LoadState = ERPGInventoryProjectionLoadState::Uninitialized;
		InventoryItems.Reset(); EquipmentItems.Reset(); MailItems.Reset();
		RefreshPendingItemCommandCount();
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LoadState);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LastCommandResult);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasLastCommandResult);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LocalActionFeedback);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RewardDeliveryMessage);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(InventoryItems);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(EquipmentItems);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(MailItems);
	}
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bInventoryAuthorityRevoked);
	RefreshPendingItemCommandCount();
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bInventoryRecoveryStorageBlocked);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasInventoryRecovery);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bCanRetryInventoryRecovery);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bInventoryRecoveryInFlight);
}

bool URPGInventoryProjectionViewModel::TickInventoryRecoveryCooldown(float)
{
	HandleInventoryRecoveryChanged(); // availability only: never submit, clear a request, or unlock inventory on a timer
	if (bHasInventoryRecovery && FPlatformTime::Seconds() < NextInventoryRecoveryRequestTime) return true;
	InventoryRecoveryCooldownTicker.Reset();
	return false;
}

bool URPGInventoryProjectionViewModel::RequestRetryInventoryCommand()
{
	auto* Commands = CommandComponent.Get();
	const auto* Controller = Commands ? Cast<APlayerController>(Commands->GetOwner()) : nullptr;
	if (IsItemAuthorityRevoked() || !Controller || Commands->IsInventoryRecoveryStorageBlocked() || !Controller->IsLocalController() || bInventoryRecoveryInFlight ||
		FPlatformTime::Seconds() < NextInventoryRecoveryRequestTime) return false;
	const auto* State = Commands->InventoryRecovery.FindByPredicate(
		[](const FRPGInventoryCommandRecovery& Entry) { return !Entry.bRetrying; });
	if (!State) return false;
	const FGuid RequestId = State->RequestId;
	NextInventoryRecoveryRequestTime = FPlatformTime::Seconds() + 3.0;
	LocalInventoryRecoveryRequest = RequestId;
	if (!InventoryRecoveryCooldownTicker.IsValid())
		InventoryRecoveryCooldownTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickInventoryRecoveryCooldown), .1f);
	HandleInventoryRecoveryChanged();
	Commands->ServerRetryUncertainInventoryCommand(RequestId);
	return true;
}

ERPGItemSlotPresentationState URPGInventoryProjectionViewModel::GetItemSlotPresentationState(
	const FGuid ItemId, const ERPGItemContainerType ContainerType, const int32 SlotIndex) const
{
	using EState = ERPGItemSlotPresentationState;
	if (IsItemAuthorityRevoked() || LoadState != ERPGInventoryProjectionLoadState::Ready ||
		!CommandComponent.IsValid() || (ContainerType != ERPGItemContainerType::Inventory &&
			ContainerType != ERPGItemContainerType::Equipment)) return EState::Unavailable;
	const auto* Commands = CommandComponent.Get();
	if (bHasInventoryRecovery || (Commands && !Commands->InventoryRecovery.IsEmpty()))
		return EState::RecoveryRequired;
	if (!ItemId.IsValid())
	{
		if (ContainerType == ERPGItemContainerType::Equipment)
		{
			if (SlotIndex < 0 || SlotIndex >= static_cast<int32>(EEquipmentSlotType::Count)) return EState::Unavailable;
			return IsEquipmentSlotReserved(SlotIndex) ? EState::Pending : EState::Idle;
		}
		if (SlotIndex < 0 || SlotIndex >= RPGItemInventoryPolicy::SlotCapacity) return EState::Unavailable;
		return IsInventorySlotReserved(SlotIndex) ? EState::Pending : EState::Idle;
	}
	FRPGInventoryProjectionEntry Entry;
	if (!TryGetEntry(ItemId, ContainerType, Entry) ||
		(SlotIndex >= 0 && Entry.GetSlotIndex() != SlotIndex)) return EState::Unavailable;
	if (IsItemActionPending(ItemId) ||
		(ContainerType == ERPGItemContainerType::Inventory && IsInventorySlotReserved(Entry.GetSlotIndex())))
		return EState::Pending;
	return Entry.IsLocked() ? EState::Locked : EState::Idle;
}

FRPGInventoryCapacityPresentation URPGInventoryProjectionViewModel::GetInventoryCapacity() const
{
	FRPGInventoryCapacityPresentation Capacity;
	Capacity.TotalSlots = RPGItemInventoryPolicy::SlotCapacity;
	Capacity.bHasSnapshot = !IsItemAuthorityRevoked() && LoadState == ERPGInventoryProjectionLoadState::Ready;
	if (!Capacity.bHasSnapshot) return Capacity;

	const auto* Commands = CommandComponent.Get();
	Capacity.bItemChangesBlocked = !PendingInventorySorts.IsEmpty() || !Commands || bHasInventoryRecovery || bInventoryRecoveryStorageBlocked ||
		(Commands && (Commands->IsInventoryRecoveryStorageBlocked() || !Commands->InventoryRecovery.IsEmpty()));
	TSet<int32> OccupiedSlots;
	for (const FRPGInventoryProjectionEntry& Entry : InventoryItems)
	{
		if (Entry.GetContainerType() == ERPGItemContainerType::Inventory &&
			Entry.GetSlotIndex() >= 0 && Entry.GetSlotIndex() < Capacity.TotalSlots)
			OccupiedSlots.Add(Entry.GetSlotIndex());
	}
	Capacity.OccupiedSlots = OccupiedSlots.Num();
	for (int32 SlotIndex = 0; SlotIndex < Capacity.TotalSlots; ++SlotIndex)
	{
		if (OccupiedSlots.Contains(SlotIndex)) continue;
		if (IsInventorySlotReserved(SlotIndex)) ++Capacity.ReservedEmptySlots;
		else ++Capacity.FreeSlots;
	}
	return Capacity;
}

void URPGInventoryProjectionViewModel::RefreshPendingItemCommandCount()
{
	const auto* Commands = CommandComponent.Get();
	const int32 NewCount = PendingInventorySorts.Num() + PendingInventoryMovesByRequest.Num() + PendingMailItemIds.Num() + PendingItemActionsByRequest.Num() +
		(!IsItemAuthorityRevoked() && Commands ? Commands->GetPendingPickupGrantCount() : 0);
	if (PendingItemCommandCount != NewCount)
	{
		PendingItemCommandCount = NewCount;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(PendingItemCommandCount);
	}
}

bool URPGInventoryProjectionViewModel::TryGetInventoryDragEntry(
	const FGuid ItemId, FRPGInventoryProjectionEntry& OutEntry) const
{
	if (bHasInventoryRecovery || LoadState != ERPGInventoryProjectionLoadState::Ready || !CommandComponent.IsValid() ||
		!ItemId.IsValid() || !TryGetEntry(ItemId, ERPGItemContainerType::Inventory, OutEntry) ||
		OutEntry.IsLocked() || OutEntry.GetQuantity() <= 0 || OutEntry.GetRevision() <= 0)
	{
		return false;
	}
	return !IsItemActionPending(ItemId);
}

bool URPGInventoryProjectionViewModel::CanMoveItemToSlot(
	const FGuid ItemId, const int64 ExpectedRevision, const int32 InventorySlotIndex) const
{
	FRPGInventoryProjectionEntry Entry;
	return InventorySlotIndex >= 0 && InventorySlotIndex < RPGItemInventoryPolicy::SlotCapacity &&
		TryGetInventoryDragEntry(ItemId, Entry) && Entry.GetRevision() == ExpectedRevision &&
		RPGItemInventoryPolicy::CanStore(Entry.GetDefinitionId(), InventorySlotIndex) &&
		Entry.GetSlotIndex() != InventorySlotIndex && !IsInventorySlotUnavailable(InventorySlotIndex);
}

bool URPGInventoryProjectionViewModel::CanSplitItem(const FGuid ItemId, const int64 ExpectedRevision,
	const int32 InventorySlotIndex, const int32 Quantity) const
{
	FRPGInventoryProjectionEntry Source;
	return Quantity > 0 && CanMoveItemToSlot(ItemId, ExpectedRevision, InventorySlotIndex) &&
		TryGetInventoryDragEntry(ItemId, Source) && Quantity < Source.GetQuantity() &&
		Source.GetSlotIndex() >= 0 && Source.GetSlotIndex() < RPGItemInventoryPolicy::SlotCapacity;
}

bool URPGInventoryProjectionViewModel::RequestSplitItem(const FGuid ItemId, const int64 ExpectedRevision,
	const int32 InventorySlotIndex, const int32 Quantity, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Source;
	auto* Commands = CommandComponent.Get();
	if (!Commands || !CanSplitItem(ItemId, ExpectedRevision, InventorySlotIndex, Quantity) ||
		!TryGetInventoryDragEntry(ItemId, Source)) return false;
	OutRequestId = FGuid::NewGuid();
	FPendingInventoryMove& Pending = PendingInventoryMovesByRequest.Add(OutRequestId);
	Pending.ItemId = ItemId;
	Pending.ExpectedRevision = ExpectedRevision;
	Pending.SourceSlot = Source.GetSlotIndex();
	Pending.DestinationSlot = InventorySlotIndex;
	Pending.SwapItemId = FRPGItemActionCommandPlanner::MakeSplitItemId(OutRequestId, ItemId);
	Pending.bWaitForCreatedItem = true;
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerSplitInventoryItem(OutRequestId, ItemId, ExpectedRevision, InventorySlotIndex, Quantity);
	return true;
}

bool URPGInventoryProjectionViewModel::RequestMoveItem(
	const FGuid ItemId, const int64 ExpectedRevision,
	const int32 InventorySlotIndex, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands || !CanMoveItemToSlot(ItemId, ExpectedRevision, InventorySlotIndex))
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	FPendingInventoryMove& Pending = PendingInventoryMovesByRequest.Add(OutRequestId);
	Pending.ItemId = ItemId;
	Pending.ExpectedRevision = ExpectedRevision;
	Pending.DestinationSlot = InventorySlotIndex;
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerMoveItem(OutRequestId, ItemId, ExpectedRevision, InventorySlotIndex);
	return true;
}

bool URPGInventoryProjectionViewModel::CanSwapItems(
	const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision) const
{
	FRPGInventoryProjectionEntry Source;
	FRPGInventoryProjectionEntry Destination;
	return SourceItemId != DestinationItemId &&
		TryGetInventoryDragEntry(SourceItemId, Source) &&
		TryGetInventoryDragEntry(DestinationItemId, Destination) &&
		Source.GetRevision() == ExpectedSourceRevision &&
		Destination.GetRevision() == ExpectedDestinationRevision &&
		Source.GetSlotIndex() >= 0 && Source.GetSlotIndex() < RPGItemInventoryPolicy::SlotCapacity &&
		Destination.GetSlotIndex() >= 0 && Destination.GetSlotIndex() < RPGItemInventoryPolicy::SlotCapacity &&
		RPGItemInventoryPolicy::CanStore(Source.GetDefinitionId(), Destination.GetSlotIndex()) &&
		RPGItemInventoryPolicy::CanStore(Destination.GetDefinitionId(), Source.GetSlotIndex()) &&
		Source.GetSlotIndex() != Destination.GetSlotIndex() &&
		!IsInventorySlotReserved(Source.GetSlotIndex()) &&
		!IsInventorySlotReserved(Destination.GetSlotIndex());
}

bool URPGInventoryProjectionViewModel::RequestSwapItems(
	const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	FRPGInventoryProjectionEntry Source;
	FRPGInventoryProjectionEntry Destination;
	if (!Commands || !CanSwapItems(SourceItemId, ExpectedSourceRevision,
		DestinationItemId, ExpectedDestinationRevision) ||
		!TryGetEntry(SourceItemId, ERPGItemContainerType::Inventory, Source) ||
		!TryGetEntry(DestinationItemId, ERPGItemContainerType::Inventory, Destination))
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	FPendingInventoryMove& Pending = PendingInventoryMovesByRequest.Add(OutRequestId);
	Pending.ItemId = SourceItemId;
	Pending.ExpectedRevision = ExpectedSourceRevision;
	Pending.DestinationSlot = Destination.GetSlotIndex();
	Pending.SwapItemId = DestinationItemId;
	Pending.ExpectedSwapRevision = ExpectedDestinationRevision;
	Pending.SourceSlot = Source.GetSlotIndex();
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerSwapInventoryItems(OutRequestId, SourceItemId, ExpectedSourceRevision,
		DestinationItemId, ExpectedDestinationRevision);
	return true;
}

ERPGInventoryOccupiedDropAction URPGInventoryProjectionViewModel::BuildOccupiedDropAction(
	const FRPGInventoryProjectionEntry& Source, const FRPGInventoryProjectionEntry& Destination,
	const FRPGItemDefinitionSnapshot* Definition)
{
	using EAction = ERPGInventoryOccupiedDropAction;
	if (Source.GetDefinitionId() != Destination.GetDefinitionId()) return EAction::Swap;
	if (!Definition || !Definition->IsValid() || Definition->DefinitionId != Source.GetDefinitionId() ||
		Definition->DefinitionVersion != Source.GetDefinitionVersion() ||
		Source.GetDefinitionVersion() != Destination.GetDefinitionVersion()) return EAction::Rejected;
	if (Definition->MaxStackSize == 1) return EAction::Swap;
	if (Source.GetQuantity() <= 0 || Source.GetQuantity() > Definition->MaxStackSize ||
		Destination.GetQuantity() <= 0 || Destination.GetQuantity() >= Definition->MaxStackSize ||
		Source.GetBindState() != Destination.GetBindState() ||
		Source.GetDurability().Current != Destination.GetDurability().Current ||
		Source.GetDurability().Maximum != Destination.GetDurability().Maximum ||
		Source.GetExpiresAtUtc() != Destination.GetExpiresAtUtc() ||
		!FRPGItemStackPolicy::AreInstanceValuesCompatible(Source.GetInstanceTags(), Source.GetRolledStats(),
			Destination.GetInstanceTags(), Destination.GetRolledStats())) return EAction::Rejected;
	return EAction::Merge;
}

ERPGInventoryOccupiedDropAction URPGInventoryProjectionViewModel::GetOccupiedDropAction(
	const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision) const
{
	if (!CanSwapItems(SourceItemId, ExpectedSourceRevision, DestinationItemId, ExpectedDestinationRevision))
		return ERPGInventoryOccupiedDropAction::Rejected;
	FRPGInventoryProjectionEntry Source;
	FRPGInventoryProjectionEntry Destination;
	if (!TryGetEntry(SourceItemId, ERPGItemContainerType::Inventory, Source) ||
		!TryGetEntry(DestinationItemId, ERPGItemContainerType::Inventory, Destination))
		return ERPGInventoryOccupiedDropAction::Rejected;
	const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	UGameInstance* GameInstance = Projection && Projection->GetWorld()
		? Projection->GetWorld()->GetGameInstance() : nullptr;
	const UDataManager* DataManager = GameInstance ? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	FRPGItemDefinitionSnapshot Definition;
	const bool bFound = DataManager && DataManager->GetItemDefinitionCatalog().TryFindDefinition(
		Source.GetDefinitionId(), Definition);
	return BuildOccupiedDropAction(Source, Destination, bFound ? &Definition : nullptr);
}

bool URPGInventoryProjectionViewModel::RequestMergeItems(
	const FGuid SourceItemId, const int64 ExpectedSourceRevision,
	const FGuid DestinationItemId, const int64 ExpectedDestinationRevision, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	FRPGInventoryProjectionEntry Source;
	FRPGInventoryProjectionEntry Destination;
	if (!Commands || GetOccupiedDropAction(SourceItemId, ExpectedSourceRevision,
		DestinationItemId, ExpectedDestinationRevision) != ERPGInventoryOccupiedDropAction::Merge ||
		!TryGetEntry(SourceItemId, ERPGItemContainerType::Inventory, Source) ||
		!TryGetEntry(DestinationItemId, ERPGItemContainerType::Inventory, Destination)) return false;
	OutRequestId = FGuid::NewGuid();
	FPendingInventoryMove& Pending = PendingInventoryMovesByRequest.Add(OutRequestId);
	Pending.ItemId = SourceItemId;
	Pending.ExpectedRevision = ExpectedSourceRevision;
	Pending.SourceSlot = Source.GetSlotIndex();
	Pending.DestinationSlot = Destination.GetSlotIndex();
	// The existing two-item reservation also covers a merge, including source removal.
	Pending.SwapItemId = DestinationItemId;
	Pending.ExpectedSwapRevision = ExpectedDestinationRevision;
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerMergeInventoryItems(OutRequestId, SourceItemId, ExpectedSourceRevision,
		DestinationItemId, ExpectedDestinationRevision);
	return true;
}

URPGInventoryProjectionViewModel::FItemPresentation URPGInventoryProjectionViewModel::GetItemPresentation(
	const FGuid ItemId) const
{
	const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	UGameInstance* GameInstance = Projection && Projection->GetWorld()
		? Projection->GetWorld()->GetGameInstance() : nullptr;
	const UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	return GetItemPresentationUsingCatalog(ItemId,
		DataManager ? &DataManager->GetItemDefinitionViewCatalog() : nullptr);
}

URPGInventoryProjectionViewModel::FItemPresentation URPGInventoryProjectionViewModel::GetItemPresentationUsingCatalog(
	const FGuid ItemId, const IRPGItemDefinitionViewCatalog* Catalog) const
{
	FItemPresentation Presentation;
	if (!ItemId.IsValid()) return Presentation;
	const auto MatchesId = [ItemId](const FRPGInventoryProjectionEntry& Entry)
		{ return Entry.GetItemId() == ItemId; };
	const FRPGInventoryProjectionEntry* Entry = InventoryItems.FindByPredicate(MatchesId);
	if (!Entry) Entry = EquipmentItems.FindByPredicate(MatchesId);
	if (!Entry) Entry = MailItems.FindByPredicate(MatchesId);
	if (!Entry) return Presentation;

	// Preserve the complete identity when metadata is missing or out of date.
	// Cutting a common tag prefix makes distinct items indistinguishable.
	Presentation.DisplayName = FText::FromName(Entry->GetDefinitionId().PrimaryAssetName);
	Presentation.Quantity = Entry->GetQuantity();
	int32 MaximumStackSize = 0;
	FRPGItemDefinitionView View;
	if (Catalog && Catalog->TryFindDefinitionView(Entry->GetDefinitionId(), View) &&
		View.IsValid() && View.Snapshot.DefinitionId == Entry->GetDefinitionId() &&
		View.Snapshot.DefinitionVersion == Entry->GetDefinitionVersion())
	{
		Presentation.DisplayName = View.DisplayName;
		Presentation.Description = View.Description;
		Presentation.Icon = View.Icon;
		MaximumStackSize = View.Snapshot.MaxStackSize;
	}
	Presentation.InstanceDetails = FRPGItemTooltipFormatter::BuildInstanceDetails(*Entry, MaximumStackSize);
	return Presentation;
}

FText URPGInventoryProjectionViewModel::GetItemDetailsText(const FGuid ItemId) const
{
	return GetItemPresentation(ItemId).GetDetailsText();
}

TSoftObjectPtr<UTexture2D> URPGInventoryProjectionViewModel::GetItemIcon(const FGuid ItemId) const
{
	return GetItemPresentation(ItemId).Icon;
}

bool URPGInventoryProjectionViewModel::RequestConsume(
	const FGuid ItemId,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (LoadState != ERPGInventoryProjectionLoadState::Ready || !Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Inventory,
			Entry) || GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Inventory) != ERPGItemSlotPresentationState::Idle)
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	FPendingItemAction& Pending = PendingItemActionsByRequest.Add(OutRequestId);
	Pending.ItemId = ItemId;
	Pending.ExpectedRevision = Entry.GetRevision();
	Pending.SourceContainer = ERPGItemContainerType::Inventory;
	Pending.SourceSlot = Entry.GetSlotIndex();
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerConsumeItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision());
	return true;
}

bool URPGInventoryProjectionViewModel::RequestDefaultAction(
	const FGuid ItemId, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Inventory) !=
		ERPGItemSlotPresentationState::Idle) return false;
	const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	UGameInstance* GameInstance = Projection && Projection->GetWorld()
		? Projection->GetWorld()->GetGameInstance() : nullptr;
	const UDataManager* DataManager = GameInstance
		? GameInstance->GetSubsystem<UDataManager>() : nullptr;
	if (!DataManager)
		return RejectLocalAction(NSLOCTEXT("RPGItemFeedback", "LocalActionDataUnavailable",
			"Action not sent: item action data is unavailable or out of date."));
	return RequestDefaultActionUsingPolicies(ItemId, DataManager->GetItemActionPolicyCatalog(), OutRequestId);
}

bool URPGInventoryProjectionViewModel::RequestDefaultActionUsingPolicies(
	const FGuid ItemId, const IRPGItemActionPolicyCatalog& Policies, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Entry;
	FRPGItemActionPolicy Policy;
	if (LoadState != ERPGInventoryProjectionLoadState::Ready ||
		!TryGetEntry(ItemId, ERPGItemContainerType::Inventory, Entry) ||
		GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Inventory) != ERPGItemSlotPresentationState::Idle)
		return false;
	if (!Policies.TryFindActionPolicy(Entry.GetDefinitionId(), Policy) || !Policy.IsValid() ||
		Policy.DefinitionId != Entry.GetDefinitionId() ||
		Policy.DefinitionVersion != Entry.GetDefinitionVersion())
	{
		return RejectLocalAction(NSLOCTEXT("RPGItemFeedback", "LocalActionDataUnavailable",
			"Action not sent: item action data is unavailable or out of date."));
	}
	if (Policy.IsConsumable())
	{
		if (Entry.GetQuantity() < Policy.QuantityPerUse)
			return RejectLocalAction(FText::Format(NSLOCTEXT("RPGItemFeedback", "LocalUseQuantityInsufficient",
				"Use not sent: one use requires {0} items. This stack has {1}."),
				FText::AsNumber(Policy.QuantityPerUse), FText::AsNumber(Entry.GetQuantity())));
		return RequestConsume(ItemId, OutRequestId);
	}
	if (!Policy.IsEquipment())
		return RejectLocalAction(NSLOCTEXT("RPGItemFeedback", "LocalNoDefaultAction",
			"Action not sent: this item has no use or equip action."));
	// Enum order is deterministic; TSet iteration order must not choose a slot.
	for (int32 Slot = 0; Slot < static_cast<int32>(EEquipmentSlotType::Count); ++Slot)
	{
		const EEquipmentSlotType SlotType = static_cast<EEquipmentSlotType>(Slot);
		if (Policy.CanEquipInSlot(SlotType) &&
			!IsEquipmentSlotUnavailable(Slot))
		{
			return RequestEquip(ItemId, SlotType, OutRequestId);
		}
	}
	return RejectLocalAction(NSLOCTEXT("RPGItemFeedback", "LocalNoFreeEquipmentSlot",
		"Equip not sent: no compatible equipment slot is free. Unequip an item or wait for pending changes."));
}

bool URPGInventoryProjectionViewModel::RequestUnequipToFirstFreeSlot(
	const FGuid ItemId, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Entry;
	if (LoadState != ERPGInventoryProjectionLoadState::Ready ||
		!TryGetEntry(ItemId, ERPGItemContainerType::Equipment, Entry) ||
		GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Equipment) != ERPGItemSlotPresentationState::Idle)
		return false;
	for (int32 Slot = 0; Slot < RPGItemInventoryPolicy::SlotsPerTab; ++Slot)
	{
		if (!IsInventorySlotUnavailable(Slot)) return RequestUnequip(ItemId, Slot, OutRequestId);
	}
	const FRPGInventoryCapacityPresentation Capacity = GetInventoryCapacityForCategory(EItemCategory::Equip);
	return RejectLocalAction(Capacity.OccupiedSlots == Capacity.TotalSlots
		? NSLOCTEXT("RPGItemFeedback", "LocalUnequipBagFull",
			"Unequip not sent: your bag is full. Free a slot first.")
		: NSLOCTEXT("RPGItemFeedback", "LocalUnequipBagReserved",
			"Unequip not sent: all empty bag slots are reserved. Wait for pending changes."));
}

bool URPGInventoryProjectionViewModel::AssignItemToQuickSlot(
	const FGuid ItemId, const int32 SlotIndex)
{
	FRPGInventoryProjectionEntry Entry;
	const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	const APlayerController* Controller = Projection
		? Cast<APlayerController>(Projection->GetOwner()) : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	UQuickSlotComponent* QuickSlots = Pawn
		? Pawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	if (!Controller || !Controller->IsLocalController() || !QuickSlots ||
		SlotIndex < 0 || SlotIndex >= QuickSlots->GetMaxItemSlots() ||
		!TryGetEntry(ItemId, ERPGItemContainerType::Inventory, Entry) ||
		Entry.GetQuantity() <= 0)
	{
		return false;
	}

	QuickSlots->SetItemSlotById(SlotIndex, Entry.GetItemId());
	return true;
}

bool URPGInventoryProjectionViewModel::RequestEquip(
	const FGuid ItemId,
	const EEquipmentSlotType SlotType,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (LoadState != ERPGInventoryProjectionLoadState::Ready ||
		static_cast<int32>(SlotType) >= static_cast<int32>(EEquipmentSlotType::Count) ||
		IsEquipmentSlotUnavailable(static_cast<int32>(SlotType)))
	{
		return false;
	}

	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Inventory,
			Entry) || GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Inventory) != ERPGItemSlotPresentationState::Idle)
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	FPendingItemAction& Pending = PendingItemActionsByRequest.Add(OutRequestId);
	Pending.ItemId = ItemId;
	Pending.ExpectedRevision = Entry.GetRevision();
	Pending.SourceContainer = ERPGItemContainerType::Inventory;
	Pending.SourceSlot = Entry.GetSlotIndex();
	Pending.DestinationContainer = ERPGItemContainerType::Equipment;
	Pending.DestinationSlot = static_cast<int32>(SlotType);
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerEquipItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision(),
		SlotType);
	return true;
}

bool URPGInventoryProjectionViewModel::RequestUnequip(
	const FGuid ItemId,
	const int32 InventorySlotIndex,
	FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (LoadState != ERPGInventoryProjectionLoadState::Ready || InventorySlotIndex < 0 ||
		InventorySlotIndex >= RPGItemInventoryPolicy::SlotCapacity ||
		IsInventorySlotUnavailable(InventorySlotIndex))
	{
		return false;
	}

	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands ||
		!TryGetEntry(
			ItemId,
			ERPGItemContainerType::Equipment,
			Entry) || !RPGItemInventoryPolicy::CanStore(Entry.GetDefinitionId(), InventorySlotIndex) ||
		GetItemSlotPresentationState(ItemId, ERPGItemContainerType::Equipment) != ERPGItemSlotPresentationState::Idle)
	{
		return false;
	}

	OutRequestId = FGuid::NewGuid();
	FPendingItemAction& Pending = PendingItemActionsByRequest.Add(OutRequestId);
	Pending.ItemId = ItemId;
	Pending.ExpectedRevision = Entry.GetRevision();
	Pending.SourceContainer = ERPGItemContainerType::Equipment;
	Pending.SourceSlot = Entry.GetSlotIndex();
	Pending.DestinationContainer = ERPGItemContainerType::Inventory;
	Pending.DestinationSlot = InventorySlotIndex;
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerUnequipItem(
		OutRequestId,
		ItemId,
		Entry.GetRevision(),
		InventorySlotIndex);
	return true;
}

bool URPGInventoryProjectionViewModel::RequestClaimMail(
	const FGuid ItemId, const int32 InventorySlotIndex, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (InventorySlotIndex < 0 || InventorySlotIndex >= RPGItemInventoryPolicy::SlotCapacity ||
		LoadState != ERPGInventoryProjectionLoadState::Ready ||
		IsMailClaimPending(ItemId) || IsInventorySlotUnavailable(InventorySlotIndex))
	{
		return false;
	}
	FRPGInventoryProjectionEntry Entry;
	URPGItemCommandComponent* Commands = CommandComponent.Get();
	if (!Commands || !TryGetEntry(ItemId, ERPGItemContainerType::Mail, Entry) ||
		!RPGItemInventoryPolicy::CanStore(Entry.GetDefinitionId(), InventorySlotIndex))
	{
		return false;
	}
	OutRequestId = FGuid::NewGuid();
	PendingMailClaimsByRequest.Add(OutRequestId, ItemId);
	PendingMailItemIds.Add(ItemId);
	PendingMailSlotsByItem.Add(ItemId, InventorySlotIndex);
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	Commands->ServerClaimMailItem(OutRequestId, ItemId, Entry.GetRevision(), InventorySlotIndex);
	return true;
}

bool URPGInventoryProjectionViewModel::IsMailClaimPending(const FGuid ItemId) const
{
	return PendingMailItemIds.Contains(ItemId);
}

ERPGMailClaimState URPGInventoryProjectionViewModel::ResolveMailClaimState(const FGuid ItemId) const
{
	using EState = ERPGMailClaimState;
	const auto* Commands = CommandComponent.Get();
	if (!ItemId.IsValid() || IsItemAuthorityRevoked() || !Commands ||
		LoadState != ERPGInventoryProjectionLoadState::Ready) return EState::Unavailable;
	if (bInventoryRecoveryStorageBlocked || Commands->IsInventoryRecoveryStorageBlocked())
		return EState::RecoveryStorageBlocked;
	if (bHasInventoryRecovery || !Commands->InventoryRecovery.IsEmpty()) return EState::RecoveryRequired;
	// Mail removal can arrive before the claimed inventory item. Keep the original reservation visible.
	if (IsMailClaimPending(ItemId)) return EState::Pending;
	FRPGInventoryProjectionEntry Entry;
	if (!TryGetEntry(ItemId, ERPGItemContainerType::Mail, Entry)) return EState::Unavailable;
	if (Entry.IsLocked()) return EState::Locked;
	const FRPGInventoryCapacityPresentation Capacity = GetInventoryCapacityForCategory(RPGItemInventoryPolicy::CategoryForDefinition(Entry.GetDefinitionId()));
	if (!Capacity.bHasSnapshot || Capacity.bItemChangesBlocked) return EState::Unavailable;
	if (Capacity.FreeSlots == 0)
		return Capacity.OccupiedSlots == Capacity.TotalSlots ? EState::InventoryFull : EState::InventoryReserved;
	return EState::Available;
}

FRPGMailClaimPresentation URPGInventoryProjectionViewModel::GetMailClaimPresentation(const FGuid ItemId) const
{
	FRPGMailClaimPresentation Presentation;
	Presentation.State = ResolveMailClaimState(ItemId);
	switch (Presentation.State)
	{
	case ERPGMailClaimState::Available:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "Claim", "Claim");
		break;
	case ERPGMailClaimState::Pending:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimPendingLabel", "Waiting");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimPendingReason",
			"Claim pending. Waiting for authoritative item updates.");
		break;
	case ERPGMailClaimState::Locked:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimLockedLabel", "Locked");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimLockedReason", "This reward is locked.");
		break;
	case ERPGMailClaimState::InventoryFull:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimFullLabel", "Full");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimFullReason",
			"Bag full. Free a slot before claiming this reward.");
		break;
	case ERPGMailClaimState::InventoryReserved:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimReservedLabel", "Reserved");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimReservedReason",
			"Empty bag slots are reserved by pending item commands.");
		break;
	case ERPGMailClaimState::RecoveryRequired:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimRecoveryLabel", "Check items");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimRecoveryReason",
			"Recover the earlier item command before claiming rewards.");
		break;
	case ERPGMailClaimState::RecoveryStorageBlocked:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimUnavailableLabel", "Unavailable");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimStorageBlockedReason",
			"Item recovery storage is unavailable. Stored commands have not been discarded.");
		break;
	default:
		Presentation.ActionLabel = NSLOCTEXT("RPGRewardMail", "ClaimUnavailableLabel", "Unavailable");
		Presentation.Reason = NSLOCTEXT("RPGRewardMail", "ClaimUnavailableReason", "Items are not ready for claiming.");
		break;
	}
	return Presentation;
}

bool URPGInventoryProjectionViewModel::RequestClaimMailToFirstFreeSlot(
	const FGuid ItemId, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	FRPGInventoryProjectionEntry Entry;
	if (!TryGetEntry(ItemId, ERPGItemContainerType::Mail, Entry)) return false;
	const int32 First = RPGItemInventoryPolicy::FirstSlot(RPGItemInventoryPolicy::CategoryForDefinition(Entry.GetDefinitionId()));
	for (int32 Slot = First; Slot < First + RPGItemInventoryPolicy::SlotsPerTab; ++Slot)
	{
		if (!IsInventorySlotUnavailable(Slot))
		{
			return RequestClaimMail(ItemId, Slot, OutRequestId);
		}
	}
	return false;
}

void URPGInventoryProjectionViewModel::HandleProjectionChanged()
{
	RefreshProjection();
}

void URPGInventoryProjectionViewModel::HandleDungeonRewardDelivered(const FRPGDungeonRewardDeliveryNotice& Notice)
{
	if (IsItemAuthorityRevoked() || !Notice.bApplied)
	{
		RewardDeliveryMessage = FText::GetEmpty();
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RewardDeliveryMessage);
		return;
	}
	RewardDeliveryMessage = FText::Format(
		NSLOCTEXT("RPGDungeonReward", "Delivered", "Rewards delivered: Inventory {0}, Mail {1}"),
		FText::AsNumber(Notice.InventoryQuantity), FText::AsNumber(Notice.MailQuantity));
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RewardDeliveryMessage);
}

void URPGInventoryProjectionViewModel::HandleLoadStateChanged(
	const ERPGInventoryProjectionLoadState NewLoadState)
{
	const auto EffectiveState = IsItemAuthorityRevoked() ? ERPGInventoryProjectionLoadState::Uninitialized : NewLoadState;
	if (LoadState != EffectiveState)
	{
		LoadState = EffectiveState;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LoadState);
	}
	RefreshProjection();
}

void URPGInventoryProjectionViewModel::HandleCommandCompleted(
	const FRPGItemCommandClientResult Result)
{
	if (IsItemAuthorityRevoked()) return; // Late ACKs cannot turn revocation into apparent recovery success.
	if (LocalInventoryRecoveryRequest.IsValid() && LocalInventoryRecoveryRequest == Result.RequestId)
	{
		LocalInventoryRecoveryRequest.Invalidate();
		HandleInventoryRecoveryChanged(); // also runs when the server's false->true->false state was coalesced
	}
	const bool bDefinitiveFailure = Result.Result != ERPGItemCommandResultCode::Succeeded &&
		Result.Result != ERPGItemCommandResultCode::AlreadyApplied &&
		Result.Result != ERPGItemCommandResultCode::Uncertain;
	const auto* Commands = CommandComponent.Get();
	const bool bRequiresRecovery = Commands && Commands->InventoryRecovery.ContainsByPredicate(
		[&Result](const FRPGInventoryCommandRecovery& Entry) { return Entry.RequestId == Result.RequestId; });
	if (auto* Sort = PendingInventorySorts.Find(Result.RequestId))
	{
		if (Result.Result == ERPGItemCommandResultCode::Succeeded || Result.Result == ERPGItemCommandResultCode::AlreadyApplied) Sort->bCommitted = true;
		else if (bDefinitiveFailure && !bRequiresRecovery) PendingInventorySorts.Remove(Result.RequestId);
	}
	ReconcilePendingInventorySorts();
	if (bDefinitiveFailure)
	{
		if (!bRequiresRecovery)
		{
			// A rejected retry does not settle the original frozen equip/unequip.
			PendingItemActionsByRequest.Remove(Result.RequestId);
			PendingInventoryMovesByRequest.Remove(Result.RequestId);
		}
	}
	else
	{
		// An RPC acknowledgement can precede the replicated location/revision.
		ReconcilePendingInventoryMoves();
		ReconcilePendingItemActions();
	}
	if (const FGuid* ClaimedItem = PendingMailClaimsByRequest.Find(Result.RequestId))
	{
		// Rejecting a changed replay does not settle the original frozen claim.
		if ((bDefinitiveFailure && !bRequiresRecovery) ||
			IsMailClaimSettledInProjection(*ClaimedItem))
		{
			PendingMailItemIds.Remove(*ClaimedItem);
			PendingMailSlotsByItem.Remove(*ClaimedItem);
			PendingMailClaimsByRequest.Remove(Result.RequestId);
		}
	}
	RefreshPendingItemCommandCount();
	LastCommandResult = Result;
	SetLocalActionFeedback(FText::GetEmpty());
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(LastCommandResult);
	if (!bHasLastCommandResult)
	{
		bHasLastCommandResult = true;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(bHasLastCommandResult);
	}
	OnCommandCompleted.Broadcast(Result);
}

bool URPGInventoryProjectionViewModel::TryGetEntry(
	const FGuid& ItemId,
	const ERPGItemContainerType ExpectedContainer,
	FRPGInventoryProjectionEntry& OutEntry) const
{
	const URPGInventoryProjectionComponent* Projection =
		ProjectionComponent.Get();
	return !IsItemAuthorityRevoked() && !bInventoryRecoveryStorageBlocked && Projection &&
		(ExpectedContainer == ERPGItemContainerType::Mail
			? Projection->FindMailItem(ItemId, OutEntry)
			: Projection->FindProjectedItem(ItemId, OutEntry)) &&
		OutEntry.GetContainerType() == ExpectedContainer;
}

bool URPGInventoryProjectionViewModel::IsInventorySlotUnavailable(
	const int32 SlotIndex) const
{
	return InventoryItems.ContainsByPredicate(
		[SlotIndex](const FRPGInventoryProjectionEntry& Entry)
		{ return Entry.GetSlotIndex() == SlotIndex; }) || IsInventorySlotReserved(SlotIndex);
}

bool URPGInventoryProjectionViewModel::IsInventorySlotReserved(const int32 SlotIndex) const
{
	if (!PendingInventorySorts.IsEmpty()) return true;
	if (const auto* Commands = CommandComponent.Get(); Commands && Commands->IsInventorySlotReservedByPickup(SlotIndex)) return true;
	for (const auto& Pair : PendingItemActionsByRequest)
	{
		const auto& Pending = Pair.Value;
		if ((Pending.SourceContainer == ERPGItemContainerType::Inventory && Pending.SourceSlot == SlotIndex) ||
			(Pending.DestinationContainer == ERPGItemContainerType::Inventory && Pending.DestinationSlot == SlotIndex)) return true;
	}
	for (const TPair<FGuid, int32>& Pending : PendingMailSlotsByItem)
	{
		if (Pending.Value == SlotIndex)
		{
			return true;
		}
	}
	for (const auto& Pending : PendingInventoryMovesByRequest)
	{
		if (Pending.Value.DestinationSlot == SlotIndex ||
			(Pending.Value.SwapItemId.IsValid() && Pending.Value.SourceSlot == SlotIndex))
		{
			return true;
		}
	}
	return false;
}

bool URPGInventoryProjectionViewModel::IsEquipmentSlotReserved(const int32 SlotIndex) const
{
	for (const auto& Pair : PendingItemActionsByRequest)
	{
		const auto& Pending = Pair.Value;
		if ((Pending.SourceContainer == ERPGItemContainerType::Equipment && Pending.SourceSlot == SlotIndex) ||
			(Pending.DestinationContainer == ERPGItemContainerType::Equipment && Pending.DestinationSlot == SlotIndex)) return true;
	}
	return false;
}

bool URPGInventoryProjectionViewModel::IsEquipmentSlotUnavailable(const int32 SlotIndex) const
{
	return EquipmentItems.ContainsByPredicate([SlotIndex](const FRPGInventoryProjectionEntry& Entry)
		{ return Entry.GetSlotIndex() == SlotIndex; }) || IsEquipmentSlotReserved(SlotIndex);
}

bool URPGInventoryProjectionViewModel::IsItemActionPending(const FGuid ItemId) const
{
	if (!PendingInventorySorts.IsEmpty()) return true;
	if (const auto* Commands = CommandComponent.Get())
		if (const auto* Entry = InventoryItems.FindByPredicate([ItemId](const auto& Item) { return Item.GetItemId() == ItemId; });
			Entry && Commands->IsInventorySlotReservedByPickup(Entry->GetSlotIndex())) return true;
	if (PendingMailItemIds.Contains(ItemId)) return true;
	for (const auto& Pair : PendingItemActionsByRequest)
		if (Pair.Value.ItemId == ItemId) return true;
	for (const auto& Pair : PendingInventoryMovesByRequest)
		if (Pair.Value.ItemId == ItemId || Pair.Value.SwapItemId == ItemId) return true;
	return false;
}

bool URPGInventoryProjectionViewModel::IsMailClaimSettledInProjection(
	const FGuid& ItemId) const
{
	return !MailItems.ContainsByPredicate(
		[ItemId](const FRPGInventoryProjectionEntry& Entry)
		{ return Entry.GetItemId() == ItemId; }) &&
		InventoryItems.ContainsByPredicate(
			[ItemId](const FRPGInventoryProjectionEntry& Entry)
			{ return Entry.GetItemId() == ItemId; });
}

void URPGInventoryProjectionViewModel::ReconcilePendingMailClaims()
{
	TArray<FGuid> CompletedMailItems;
	for (const FGuid& PendingItemId : PendingMailItemIds)
	{
		if (IsMailClaimSettledInProjection(PendingItemId))
		{
			CompletedMailItems.Add(PendingItemId);
		}
	}
	for (const FGuid& CompletedItemId : CompletedMailItems)
	{
		PendingMailItemIds.Remove(CompletedItemId);
		PendingMailSlotsByItem.Remove(CompletedItemId);
		for (auto It = PendingMailClaimsByRequest.CreateIterator(); It; ++It)
			if (It.Value() == CompletedItemId) It.RemoveCurrent();
	}
}

void URPGInventoryProjectionViewModel::ReconcilePendingInventoryMoves()
{
	const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
	if (!Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready)
	{
		return;
	}
	const auto IsSnapshotRetired = [Projection](const FGuid& ItemId, const int64 ExpectedRevision)
	{
		FRPGInventoryProjectionEntry Entry;
		return !Projection->FindProjectedItem(ItemId, Entry) || Entry.GetRevision() > ExpectedRevision;
	};
	for (auto It = PendingInventoryMovesByRequest.CreateIterator(); It; ++It)
	{
		const FPendingInventoryMove& Pending = It.Value();
		// A newer authoritative revision (or removal) retires this drag snapshot,
		// even if another command won the race. Never move UI entries locally.
		// A swap/merge holds both reservations until BOTH item snapshots are retired.
		FRPGInventoryProjectionEntry Created;
		const bool bSecondReady = Pending.bWaitForCreatedItem
			? Projection->FindProjectedItem(Pending.SwapItemId, Created) && Created.GetRevision() > 0 &&
				Created.GetContainerType() == ERPGItemContainerType::Inventory && Created.GetSlotIndex() == Pending.DestinationSlot
			: !Pending.SwapItemId.IsValid() || IsSnapshotRetired(Pending.SwapItemId, Pending.ExpectedSwapRevision);
		if (IsSnapshotRetired(Pending.ItemId, Pending.ExpectedRevision) && bSecondReady)
		{
			It.RemoveCurrent();
		}
	}
}

void URPGInventoryProjectionViewModel::ReconcilePendingItemActions()
{
	const auto* Projection = ProjectionComponent.Get();
	if (!Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready) return;
	for (auto It = PendingItemActionsByRequest.CreateIterator(); It; ++It)
	{
		FRPGInventoryProjectionEntry Entry;
		// Each action mutates one identity. A newer authoritative revision/removal retires
		// the old snapshot, even if a competing command won. ACKs alone never do this.
		if (!Projection->FindProjectedItem(It.Value().ItemId, Entry) || Entry.GetRevision() > It.Value().ExpectedRevision)
			It.RemoveCurrent();
	}
}

void URPGInventoryProjectionViewModel::RefreshProjection()
{
	TArray<FRPGInventoryProjectionEntry> NewInventoryItems;
	TArray<FRPGInventoryProjectionEntry> NewEquipmentItems;
	TArray<FRPGInventoryProjectionEntry> NewMailItems;
	if (const URPGInventoryProjectionComponent* Projection = ProjectionComponent.Get();
		Projection && !IsItemAuthorityRevoked())
	{
		NewMailItems = Projection->GetMailItems();
		for (const FRPGInventoryProjectionEntry& Entry :
			Projection->GetProjectedItems())
		{
			switch (Entry.GetContainerType())
			{
			case ERPGItemContainerType::Inventory:
				NewInventoryItems.Add(Entry);
				break;
			case ERPGItemContainerType::Equipment:
				NewEquipmentItems.Add(Entry);
				break;
			default:
				break;
			}
		}
	}

	InventoryItems = MoveTemp(NewInventoryItems);
	EquipmentItems = MoveTemp(NewEquipmentItems);
	MailItems = MoveTemp(NewMailItems);
	ReconcilePendingInventorySorts();
	ReconcilePendingMailClaims();
	ReconcilePendingInventoryMoves();
	ReconcilePendingItemActions();
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(InventoryItems);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(EquipmentItems);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(MailItems);
}

void URPGInventoryProjectionViewModel::UnbindComponents()
{
	FTSTicker::GetCoreTicker().RemoveTicker(InventoryRecoveryCooldownTicker);
	InventoryRecoveryCooldownTicker.Reset();
	LocalInventoryRecoveryRequest.Invalidate();
	NextInventoryRecoveryRequestTime = 0.0;
	if (ARPGPlayerState* State = RewardPlayerState.Get())
	{
		State->OnDungeonRewardDelivered.RemoveDynamic(this, &ThisClass::HandleDungeonRewardDelivered);
	}
	RewardPlayerState.Reset();
	if (URPGInventoryProjectionComponent* Projection =
		ProjectionComponent.Get())
	{
		Projection->OnProjectionChanged.RemoveDynamic(
			this,
			&ThisClass::HandleProjectionChanged);
		Projection->OnLoadStateChanged.RemoveDynamic(
			this,
			&ThisClass::HandleLoadStateChanged);
	}
	if (URPGItemCommandComponent* Commands = CommandComponent.Get())
	{
		Commands->OnInventoryRecoveryChanged.RemoveDynamic(this, &ThisClass::HandleInventoryRecoveryChanged);
		Commands->OnItemCommandCompleted.RemoveDynamic(
			this,
			&ThisClass::HandleCommandCompleted);
	}
	ProjectionComponent.Reset();
	CommandComponent.Reset();
	BoundPlayerController.Reset();
}

FRPGInventoryCapacityPresentation URPGInventoryProjectionViewModel::GetInventoryCapacityForCategory(EItemCategory Category) const
{
	auto Capacity = GetInventoryCapacity();
	Capacity.TotalSlots = RPGItemInventoryPolicy::SlotsPerTab;
	Capacity.OccupiedSlots = Capacity.FreeSlots = Capacity.ReservedEmptySlots = 0;
	if (!RPGItemInventoryPolicy::IsTab(Category)) { Capacity.bHasSnapshot = false; return Capacity; }
	if (!Capacity.bHasSnapshot) return Capacity;
	for (int32 Slot = RPGItemInventoryPolicy::FirstSlot(Category); Slot < RPGItemInventoryPolicy::FirstSlot(Category) + Capacity.TotalSlots; ++Slot)
	{
		if (InventoryItems.ContainsByPredicate([Slot](const auto& Item) { return Item.GetSlotIndex() == Slot; })) ++Capacity.OccupiedSlots;
		else if (IsInventorySlotReserved(Slot)) ++Capacity.ReservedEmptySlots;
		else ++Capacity.FreeSlots;
	}
	return Capacity;
}
bool URPGInventoryProjectionViewModel::CanSortInventoryTab(EItemCategory Category) const
{
	const auto* Commands = CommandComponent.Get();
	const auto Capacity = GetInventoryCapacityForCategory(Category);
	return Capacity.bHasSnapshot && !Capacity.bItemChangesBlocked && Commands && !Commands->HasInFlightCommands() &&
		PendingItemCommandCount == 0 && Capacity.OccupiedSlots > 0;
}
bool URPGInventoryProjectionViewModel::RequestSortInventoryTab(EItemCategory Category, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	if (!CanSortInventoryTab(Category)) return false;
	OutRequestId = FGuid::NewGuid();
	auto& Pending = PendingInventorySorts.Add(OutRequestId);
	for (const auto& Item : InventoryItems)
		if (RPGItemInventoryPolicy::Contains(Category, Item.GetSlotIndex()) && !Item.IsLocked()) Pending.Revisions.Add(Item.GetItemId(), Item.GetRevision());
	RefreshPendingItemCommandCount();
	SetLocalActionFeedback(FText::GetEmpty());
	CommandComponent->ServerSortInventoryTab(OutRequestId, Category);
	return true;
}
void URPGInventoryProjectionViewModel::ReconcilePendingInventorySorts()
{
	for (auto It = PendingInventorySorts.CreateIterator(); It; ++It)
	{
		if (!It.Value().bCommitted) continue;
		bool bReady = true;
		for (const auto& Expected : It.Value().Revisions)
		{
			const auto* Entry = InventoryItems.FindByPredicate([&Expected](const auto& Item) { return Item.GetItemId() == Expected.Key; });
			if (!Entry || Entry->GetRevision() <= Expected.Value) { bReady = false; break; }
		}
		if (bReady) It.RemoveCurrent();
	}
}
