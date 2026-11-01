#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Item/Transaction/RPGItemClock.h"
#include "Item/Transaction/RPGItemTransactionTypes.h"

class IRPGItemDefinitionCatalog;
class IRPGItemActionPolicyCatalog;
class IRPGItemRepository;
struct FRPGItemRepositoryCommitResult;

/**
 * Synchronous item transaction reference service for automation fixtures only.
 *
 * Callers provide the authenticated actor and expected revisions. The service
 * owns validation and delegates only the atomic compare-and-swap commit to the
 * repository. Online gameplay uses the async command orchestrator and backend.
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

#endif // WITH_DEV_AUTOMATION_TESTS
