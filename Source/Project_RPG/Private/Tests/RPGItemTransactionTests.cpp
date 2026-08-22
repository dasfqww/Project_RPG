#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Persistence/RPGInMemoryItemRepository.h"
#include "Item/Transaction/RPGItemTransactionService.h"

namespace RPGItemTransactionTests
{
URPGItemDefinition* MakeDefinition(
	const TCHAR* BaseName,
	const int32 MaxStackSize = 10,
	const int32 DefinitionVersion = 1)
{
	const FName ObjectName = MakeUniqueObjectName(
		GetTransientPackage(),
		URPGItemDefinition::StaticClass(),
		FName(BaseName));
	URPGItemDefinition* Definition =
		NewObject<URPGItemDefinition>(GetTransientPackage(), ObjectName);
	Definition->DefinitionVersion = DefinitionVersion;
	Definition->MaxStackSize = MaxStackSize;
	return Definition;
}

FRPGItemOwnerRef MakeOwner(const TCHAR* OwnerId = TEXT("character:test"))
{
	FRPGItemOwnerRef Owner;
	Owner.Type = ERPGItemOwnerType::Character;
	Owner.OwnerId = OwnerId;
	return Owner;
}

FRPGItemLocation MakeInventoryLocation(const int32 SlotIndex)
{
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory;
	Location.ContainerId = TEXT("inventory:main");
	Location.SlotIndex = SlotIndex;
	return Location;
}

bool MakeRecord(
	const URPGItemDefinition& Definition,
	const FRPGItemOwnerRef& Owner,
	const int32 SlotIndex,
	const int32 Quantity,
	const int32 Seed,
	FRPGItemRecord& OutRecord)
{
	FRPGItemInstanceState State;
	Definition.BuildInstanceState(Quantity, Seed, State);

	FRPGItemRecordMetadata Metadata;
	return FRPGItemRecord::TryCreate(
		Definition.GetPrimaryAssetId(),
		Definition.GetDefinitionVersion(),
		Owner,
		MakeInventoryLocation(SlotIndex),
		State,
		Metadata,
		OutRecord);
}

FRPGItemRepositoryCommitResult SeedRecord(
	FRPGInMemoryItemRepository& Repository,
	const FRPGItemRecord& Record)
{
	FRPGItemRepositoryCommitRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Operation = TEXT("Test.Seed");
	Request.CommandFingerprint = Record.GetItemId().ToString(
		EGuidFormats::DigitsWithHyphens);
	Request.Actor = Record.GetOwner();
	Request.AffectedQuantity = Record.GetQuantity();

	FRPGItemRecordMutation& Mutation =
		Request.Mutations.AddDefaulted_GetRef();
	Mutation.ExpectedRevision = 0;
	Mutation.NewRecord = Record;
	return Repository.Commit(Request);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemMoveTransactionTest,
	"ProjectRPG.Item.Transaction.MoveIsRevisionedAndIdempotent",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemMoveTransactionTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemTransactionTests;

	URPGItemDefinition* Definition =
		MakeDefinition(TEXT("MoveTransactionDefinition"));
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRPGInMemoryItemRepository Repository;

	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord NewRecord;
	TestTrue(TEXT("Record is valid before insertion"),
		MakeRecord(*Definition, Owner, 0, 4, 100, NewRecord));
	TestTrue(TEXT("Seed commit succeeds"),
		SeedRecord(Repository, NewRecord).WasApplied());

	FRPGItemTransactionService Service(Repository, Definitions);
	FRPGItemMoveRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Actor = Owner;
	Request.ItemId = NewRecord.GetItemId();
	Request.ExpectedRevision = 1;
	Request.Destination = MakeInventoryLocation(5);

	const FRPGItemTransactionResult FirstResult =
		Service.MoveItem(Request);
	TestTrue(TEXT("Move succeeds"),
		FirstResult.Status == ERPGItemTransactionStatus::Succeeded);
	TestEqual(TEXT("The whole stack is affected"),
		FirstResult.AffectedQuantity, 4);

	const FRPGItemRecord* FirstWrittenRecord =
		FirstResult.FindRecord(NewRecord.GetItemId());
	TestNotNull(TEXT("Move returns the written record"), FirstWrittenRecord);
	if (FirstWrittenRecord)
	{
		TestEqual(TEXT("Revision increments once"),
			FirstWrittenRecord->GetRevision(), static_cast<int64>(2));
		TestEqual(TEXT("Destination slot is persisted"),
			FirstWrittenRecord->GetLocation().SlotIndex, 5);
	}

	const FRPGItemTransactionResult ReplayResult =
		Service.MoveItem(Request);
	TestTrue(TEXT("Retry returns the cached receipt"),
		ReplayResult.Status == ERPGItemTransactionStatus::AlreadyApplied);
	TestEqual(TEXT("Retry reports the original affected quantity"),
		ReplayResult.AffectedQuantity, 4);

	FRPGItemMoveRequest ReusedIdRequest = Request;
	ReusedIdRequest.Destination = MakeInventoryLocation(6);
	TestTrue(TEXT("Request ID reuse with another command is rejected"),
		Service.MoveItem(ReusedIdRequest).Status ==
			ERPGItemTransactionStatus::IdempotencyConflict);

	FRPGItemRecord StoredRecord;
	TestTrue(TEXT("Moved item remains stored"),
		Repository.Find(NewRecord.GetItemId(), StoredRecord));
	TestEqual(TEXT("Retry does not increment revision again"),
		StoredRecord.GetRevision(), static_cast<int64>(2));
	TestEqual(TEXT("Retry does not move the item again"),
		StoredRecord.GetLocation().SlotIndex, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemStackTransactionTest,
	"ProjectRPG.Item.Transaction.StackTransferIsAtomic",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemStackTransactionTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemTransactionTests;

	URPGItemDefinition* Definition =
		MakeDefinition(TEXT("StackTransactionDefinition"), 10);
	FRPGItemDefinitionRegistry Definitions;
	TestTrue(TEXT("Definition registers"),
		Definitions.RegisterDefinition(*Definition));
	FRPGInMemoryItemRepository Repository;

	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord Source;
	FRPGItemRecord Destination;
	TestTrue(TEXT("Source record is created"),
		MakeRecord(*Definition, Owner, 0, 3, 777, Source));
	TestTrue(TEXT("Destination record is created"),
		MakeRecord(*Definition, Owner, 1, 7, 777, Destination));
	TestTrue(TEXT("Source is inserted"),
		SeedRecord(Repository, Source).WasApplied());
	TestTrue(TEXT("Destination is inserted"),
		SeedRecord(Repository, Destination).WasApplied());

	FRPGItemTransactionService Service(Repository, Definitions);
	FRPGItemStackTransferRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.Actor = Owner;
	Request.SourceItemId = Source.GetItemId();
	Request.ExpectedSourceRevision = 1;
	Request.DestinationItemId = Destination.GetItemId();
	Request.ExpectedDestinationRevision = 1;
	Request.RequestedQuantity = 5;

	const FRPGItemTransactionResult Result =
		Service.TransferStack(Request);
	TestTrue(TEXT("Stack transaction succeeds"),
		Result.Status == ERPGItemTransactionStatus::Succeeded);
	TestEqual(TEXT("Transfer is capped by target capacity"),
		Result.AffectedQuantity, 3);

	const FRPGItemRecord* NewSource =
		Result.FindRecord(Source.GetItemId());
	const FRPGItemRecord* NewDestination =
		Result.FindRecord(Destination.GetItemId());
	TestNotNull(TEXT("Source result is returned"), NewSource);
	TestNotNull(TEXT("Destination result is returned"), NewDestination);
	if (NewSource && NewDestination)
	{
		TestEqual(TEXT("Consumed source reaches zero"),
			NewSource->GetQuantity(), 0);
		TestTrue(TEXT("Consumed source becomes a terminal tombstone"),
			NewSource->GetLifecycleState() ==
				ERPGItemLifecycleState::Consumed &&
			NewSource->GetLocation().IsTerminalLocation());
		TestEqual(TEXT("Destination reaches max stack"),
			NewDestination->GetQuantity(), 10);
		TestEqual(TEXT("Source revision increments"),
			NewSource->GetRevision(), static_cast<int64>(2));
		TestEqual(TEXT("Destination revision increments"),
			NewDestination->GetRevision(), static_cast<int64>(2));
	}

	const FRPGItemTransactionResult Replay =
		Service.TransferStack(Request);
	TestTrue(TEXT("Stack retry is idempotent"),
		Replay.Status == ERPGItemTransactionStatus::AlreadyApplied);
	TestEqual(TEXT("Stack retry retains original quantity"),
		Replay.AffectedQuantity, 3);

	FRPGItemRecord StoredSource;
	FRPGItemRecord StoredDestination;
	Repository.Find(Source.GetItemId(), StoredSource);
	Repository.Find(Destination.GetItemId(), StoredDestination);
	TestEqual(TEXT("Replay leaves consumed source unchanged"),
		StoredSource.GetQuantity(), 0);
	TestEqual(TEXT("Replay leaves destination unchanged"),
		StoredDestination.GetQuantity(), 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGItemTransactionConflictTest,
	"ProjectRPG.Item.Transaction.RejectsConflictsAndUnauthorizedActors",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGItemTransactionConflictTest::RunTest(const FString& Parameters)
{
	using namespace RPGItemTransactionTests;

	URPGItemDefinition* Definition =
		MakeDefinition(TEXT("ConflictTransactionDefinition"));
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRPGInMemoryItemRepository Repository;

	const FRPGItemOwnerRef Owner = MakeOwner();
	FRPGItemRecord Source;
	FRPGItemRecord Occupant;
	MakeRecord(*Definition, Owner, 0, 1, 10, Source);
	MakeRecord(*Definition, Owner, 1, 1, 20, Occupant);
	SeedRecord(Repository, Source);
	SeedRecord(Repository, Occupant);

	FRPGItemTransactionService Service(Repository, Definitions);

	FRPGItemMoveRequest UnauthorizedRequest;
	UnauthorizedRequest.RequestId = FGuid::NewGuid();
	UnauthorizedRequest.Actor = MakeOwner(TEXT("character:other"));
	UnauthorizedRequest.ItemId = Source.GetItemId();
	UnauthorizedRequest.ExpectedRevision = 1;
	UnauthorizedRequest.Destination = MakeInventoryLocation(2);
	TestTrue(TEXT("Another owner cannot move the item"),
		Service.MoveItem(UnauthorizedRequest).Status ==
			ERPGItemTransactionStatus::Forbidden);

	FRPGItemMoveRequest OccupiedRequest;
	OccupiedRequest.RequestId = FGuid::NewGuid();
	OccupiedRequest.Actor = Owner;
	OccupiedRequest.ItemId = Source.GetItemId();
	OccupiedRequest.ExpectedRevision = 1;
	OccupiedRequest.Destination = MakeInventoryLocation(1);
	TestTrue(TEXT("Occupied slot is rejected"),
		Service.MoveItem(OccupiedRequest).Status ==
			ERPGItemTransactionStatus::LocationOccupied);

	FRPGItemMoveRequest ValidRequest = OccupiedRequest;
	ValidRequest.RequestId = FGuid::NewGuid();
	ValidRequest.Destination = MakeInventoryLocation(2);
	TestTrue(TEXT("Valid move succeeds"),
		Service.MoveItem(ValidRequest).WasSuccessful());

	FRPGItemMoveRequest StaleRequest = ValidRequest;
	StaleRequest.RequestId = FGuid::NewGuid();
	StaleRequest.Destination = MakeInventoryLocation(3);
	TestTrue(TEXT("Stale expected revision is rejected"),
		Service.MoveItem(StaleRequest).Status ==
			ERPGItemTransactionStatus::RevisionConflict);

	FRPGItemRecord StoredSource;
	Repository.Find(Source.GetItemId(), StoredSource);
	TestEqual(TEXT("Failed requests do not advance revision"),
		StoredSource.GetRevision(), static_cast<int64>(2));
	TestEqual(TEXT("Failed requests do not alter location"),
		StoredSource.GetLocation().SlotIndex, 2);
	return true;
}

#endif
