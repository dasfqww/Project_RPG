#pragma once

#include "Item/Backend/RPGItemBackendTypes.h"

enum class ERPGPickupGrantReceiptDecision : uint8
{
	RetrySameCommit,
	ReleasePickup,
	ApplyReceipt
};

/** Classifies a frozen pickup grant receipt before any world or UI mutation. */
class RPGINVENTORYRUNTIME_API FRPGPickupGrantReceiptPolicy final
{
public:
	static ERPGPickupGrantReceiptDecision Evaluate(
		const FRPGItemRepositoryCommitRequest& Request,
		const FRPGItemBackendCommitResult& Result,
		bool bPreserveReservation = false);
};
