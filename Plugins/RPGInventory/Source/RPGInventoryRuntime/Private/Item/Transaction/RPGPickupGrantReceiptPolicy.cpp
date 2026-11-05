#include "Item/Transaction/RPGPickupGrantReceiptPolicy.h"

ERPGPickupGrantReceiptDecision FRPGPickupGrantReceiptPolicy::Evaluate(
	const FRPGItemRepositoryCommitRequest& Request,
	const FRPGItemBackendCommitResult& Result,
	const bool bPreserveReservation)
{
	if (Request.Mutations.Num() != 1 ||
		Result.RequestId != Request.RequestId || Result.Operation != Request.Operation ||
		Result.CommandFingerprint != Request.CommandFingerprint || Result.Actor != Request.Actor)
	{
		return ERPGPickupGrantReceiptDecision::RetrySameCommit;
	}
	if (Result.WasSuccessful())
	{
		if (Result.Records.Num() != 1 || Result.AffectedQuantity != Request.AffectedQuantity ||
			Request.Mutations[0].ExpectedRevision != 0)
		{
			return ERPGPickupGrantReceiptDecision::RetrySameCommit;
		}
		const FRPGItemRecord Expected = Request.Mutations[0].NewRecord.CopyWithRevision(1);
		if (!FRPGItemRecord::StaticStruct()->CompareScriptStruct(
			&Expected, &Result.Records[0], 0))
		{
			return ERPGPickupGrantReceiptDecision::RetrySameCommit;
		}
		return ERPGPickupGrantReceiptDecision::ApplyReceipt;
	}
	// A prepared origin has no release/retarget protocol. Even a correlated
	// slot/revision rejection must retain its original claimant and command.
	if (bPreserveReservation) return ERPGPickupGrantReceiptDecision::RetrySameCommit;
	// The gateway may already have retried a lost response before returning an
	// HTTP/authentication error. Such errors cannot prove that no grant was saved.
	// Only a correlated repository rejection permits another pickup attempt.
	switch (Result.Status)
	{
	case ERPGItemBackendStatus::RevisionConflict:
	case ERPGItemBackendStatus::LocationConflict:
	case ERPGItemBackendStatus::ValidationFailed:
		return ERPGPickupGrantReceiptDecision::ReleasePickup;
	default:
		return ERPGPickupGrantReceiptDecision::RetrySameCommit;
	}
}
