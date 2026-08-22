#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Type/RPGEnumTypes.h"

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

struct PROJECT_RPG_API FRPGItemMoveRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation Destination;
};

struct PROJECT_RPG_API FRPGItemStackTransferRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid SourceItemId;
	int64 ExpectedSourceRevision = 0;
	FGuid DestinationItemId;
	int64 ExpectedDestinationRevision = 0;
	int32 RequestedQuantity = 0;
};

struct PROJECT_RPG_API FRPGItemEquipRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FString EquipmentContainerId;
	EEquipmentSlotType SlotType = EEquipmentSlotType::None;
};

struct PROJECT_RPG_API FRPGItemUnequipRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
	FRPGItemLocation InventoryDestination;
};

struct PROJECT_RPG_API FRPGItemConsumeRequest
{
	FGuid RequestId;
	FRPGItemOwnerRef Actor;
	FGuid ItemId;
	int64 ExpectedRevision = 0;
};

struct PROJECT_RPG_API FRPGItemTransactionResult
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
