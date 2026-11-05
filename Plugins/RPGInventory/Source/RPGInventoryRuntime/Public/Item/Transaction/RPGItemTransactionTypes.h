#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Item/RPGInventoryTypes.h"

enum class ERPGItemTransactionStatus : uint8
{
	Succeeded,
	AlreadyApplied,
	InvalidRequest,
	IdempotencyConflict,
	NotFound,
	Forbidden,
	Locked,
	Expired,
	NotActive,
	RevisionConflict,
	LocationOccupied,
	IncompatibleStack,
	DefinitionUnavailable,
	DefinitionVersionMismatch,
	StackFull,
	InvalidContainerTransition,
	NotEquippable,
	IncompatibleEquipmentSlot,
	NotConsumable,
	InsufficientQuantity,
	NoChange,
	RepositoryRejected
};

struct RPGINVENTORYRUNTIME_API FRPGItemMoveRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation Destination;
};

/** Keeps the original stack and inserts a new stack into an empty bag slot. */
struct RPGINVENTORYRUNTIME_API FRPGItemSplitRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation Destination;
	int32 Quantity = 0;
};

/** Exchanges two complete stacks in the same character inventory. */
struct RPGINVENTORYRUNTIME_API FRPGItemSwapRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid SourceItemId;
	int64 ExpectedSourceRevision = 0;
	FGuid DestinationItemId;
	int64 ExpectedDestinationRevision = 0;
	FString InventoryContainerId;
};

struct RPGINVENTORYRUNTIME_API FRPGItemStackTransferRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid SourceItemId;
	int64 ExpectedSourceRevision = 0;
	FGuid DestinationItemId;
	int64 ExpectedDestinationRevision = 0;
	int32 RequestedQuantity = 0;
};

struct RPGINVENTORYRUNTIME_API FRPGItemEquipRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FString EquipmentContainerId;
	EEquipmentSlotType SlotType = EEquipmentSlotType::None;
};

struct RPGINVENTORYRUNTIME_API FRPGItemUnequipRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation InventoryDestination;
};

struct RPGINVENTORYRUNTIME_API FRPGItemConsumeRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
};

/** Moves an existing attachment out of mail; never mints a replacement item. */
struct RPGINVENTORYRUNTIME_API FRPGItemClaimMailRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation InventoryDestination;
};

struct RPGINVENTORYRUNTIME_API FRPGItemTransactionResult
{
	ERPGItemTransactionStatus Status =
		ERPGItemTransactionStatus::RepositoryRejected;
	FGuid RequestId;
	int32 AffectedQuantity = 0;
	TArray<FRPGItemRecord> Records;

	bool WasSuccessful() const
	{
		return Status == ERPGItemTransactionStatus::Succeeded ||
			Status == ERPGItemTransactionStatus::AlreadyApplied;
	}

	const FRPGItemRecord* FindRecord(const FGuid& ItemId) const
	{
		return Records.FindByPredicate(
			[&ItemId](const FRPGItemRecord& Record)
			{
				return Record.GetItemId() == ItemId;
			});
	}
};
