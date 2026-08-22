#pragma once

#include "CoreMinimal.h"
#include "Item/Transaction/RPGItemClock.h"
#include "Item/Transaction/RPGItemTransactionTypes.h"

class IRPGItemDefinitionCatalog;
class IRPGItemActionPolicyCatalog;
class IRPGItemRepository;
struct FRPGItemRepositoryCommitResult;

/**
 * Authoritative application service for item state transitions.
 *
 * Callers provide the authenticated actor and expected revisions. The service
 * owns validation and delegates only the atomic compare-and-swap commit to the
 * repository.
 */
class PROJECT_RPG_API FRPGItemTransactionService
{
public:
	FRPGItemTransactionService(
		IRPGItemRepository& InRepository,
		const IRPGItemDefinitionCatalog& InDefinitionCatalog);

	FRPGItemTransactionService(
		IRPGItemRepository& InRepository,
		const IRPGItemDefinitionCatalog& InDefinitionCatalog,
		const IRPGItemClock& InClock);
	FRPGItemTransactionService(
		IRPGItemRepository& InRepository,
		const IRPGItemDefinitionCatalog& InDefinitionCatalog,
		const IRPGItemActionPolicyCatalog& InActionPolicyCatalog);
	FRPGItemTransactionService(
		IRPGItemRepository& InRepository,
		const IRPGItemDefinitionCatalog& InDefinitionCatalog,
		const IRPGItemActionPolicyCatalog& InActionPolicyCatalog,
		const IRPGItemClock& InClock);

	FRPGItemTransactionResult MoveItem(
		const FRPGItemMoveRequest& Request);
	FRPGItemTransactionResult TransferStack(
		const FRPGItemStackTransferRequest& Request);
	FRPGItemTransactionResult EquipItem(
		const FRPGItemEquipRequest& Request);
	FRPGItemTransactionResult UnequipItem(
		const FRPGItemUnequipRequest& Request);
	FRPGItemTransactionResult ConsumeItem(
		const FRPGItemConsumeRequest& Request);

private:
	bool TryReplay(
		const FGuid& RequestId,
		const FRPGItemOwnerRef& Actor,
		const FName& Operation,
		const FString& CommandFingerprint,
		FRPGItemTransactionResult& OutResult) const;

	static FRPGItemTransactionResult FromCommitResult(
		const FRPGItemRepositoryCommitResult& CommitResult);
	static FRPGItemTransactionResult MakeFailure(
		const FGuid& RequestId,
		ERPGItemTransactionStatus Status);

	IRPGItemRepository& Repository;
	const IRPGItemDefinitionCatalog& DefinitionCatalog;
	const IRPGItemActionPolicyCatalog* ActionPolicyCatalog = nullptr;
	FRPGSystemItemClock SystemClock;
	const IRPGItemClock* Clock = nullptr;
};
