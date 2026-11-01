#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Component/RPGPickupGrantComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Transaction/RPGItemPickupGrantPlanner.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "RPGItemTags.h"
#include "Player/RPGPlayerState.h"
#include "TimerManager.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UObject/UnrealType.h"

struct FRPGPickupSlotReservationTestAccess
{
	static TUniquePtr<IRPGItemAsyncCommitter> Wrap(URPGItemCommandComponent& Commands, TUniquePtr<IRPGItemAsyncCommitter> Inner)
	{ return Commands.MakeSlotAwareCommitter(MoveTemp(Inner)); }
	static void Bind(URPGItemCommandComponent& Commands, TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> Pipeline)
	{ Commands.Orchestrator = MoveTemp(Pipeline); }
	static void Finish(URPGItemCommandComponent& Commands, FGuid Request, FName Operation, FRPGItemAsyncCommandOutcome Outcome)
	{ Commands.HandleOutcome(Request, Operation, MoveTemp(Outcome)); }
	static int32 InventoryRequests(const URPGItemCommandComponent& Commands) { return Commands.PendingInventorySlotRequests.Num(); }
	static bool Track(URPGItemCommandComponent& Commands, const FRPGItemRepositoryCommitRequest& Request)
	{ return Commands.TrackInventorySlotRequest(Request); }
	static bool Plan(const URPGItemCommandComponent& Commands, const FRPGItemPickupGrantRequest& Request,
		const URPGItemDefinition& Definition, const IRPGItemRecordSource& Records,
		const IRPGItemDefinitionCatalog& Definitions, FRPGItemRepositoryCommitRequest& OutCommit)
	{
		return Commands.TryPlanPickupGrant(Request, Definition, Records, Definitions, OutCommit);
	}
};

namespace RPGPickupGrantRuntimeTests
{
class FDeferredCommitter final : public IRPGItemAsyncCommitter
{
public:
	TArray<FRPGItemRepositoryCommitRequest> Requests;
	FRPGItemBackendCommitCompletion Pending;

	virtual bool Commit(const FRPGItemRepositoryCommitRequest& Request,
		FRPGItemBackendCommitCompletion Completion) override
	{
		Requests.Add(Request);
		Pending = MoveTemp(Completion);
		return true;
	}
	void Reply(ERPGItemBackendStatus Status, bool bWrongQuantity = false)
	{
		const FRPGItemRepositoryCommitRequest& Request = Requests.Last();
		FRPGItemBackendCommitResult Receipt;
		Receipt.Status = Status;
		Receipt.RequestId = Request.RequestId;
		Receipt.Operation = Request.Operation;
		Receipt.CommandFingerprint = Request.CommandFingerprint;
		Receipt.Actor = Request.Actor;
		Receipt.AffectedQuantity = Request.AffectedQuantity;
		if (Receipt.WasSuccessful())
		{
			for (const auto& Mutation : Request.Mutations)
				Receipt.Records.Add(Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1));
			if (bWrongQuantity) ++Receipt.AffectedQuantity;
		}
		FRPGItemBackendCommitCompletion Callback = MoveTemp(Pending);
		Callback(MoveTemp(Receipt));
	}
};

struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	ARPGPlayerState* PlayerState = nullptr;
	ARPGPickUpBase* Pickup = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGItemCommandComponent* Commands = nullptr;
	URPGPickupGrantComponent* Grant = nullptr;
	TSharedRef<FDeferredCommitter> Committer = MakeShared<FDeferredCommitter>();
	FRPGItemRepositoryCommitRequest Request;

	FFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		PlayerState = World->SpawnActor<ARPGPlayerState>();
		Pickup = World->SpawnActor<ARPGPickUpBase>();
		if (!Controller || !PlayerState || !Pickup) return;
		Controller->SetPlayerState(PlayerState);
		Request.RequestId = FGuid::NewGuid();
		Request.Operation = TEXT("Item.Pickup");
		Request.CommandFingerprint = TEXT("pickup-runtime-test");
		Request.Actor.Type = ERPGItemOwnerType::Character;
		Request.Actor.OwnerId = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
		Request.AffectedQuantity = 3;
		PlayerState->SetAuthenticatedIdentity(Request.Actor.OwnerId,
			TEXT("76561198000000001"), TEXT("pickup-test-session"));
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
		Controller->AddInstanceComponent(Projection);
		Projection->RegisterComponent();
		Projection->ApplyAuthoritativeRecords(Request.Actor, {});
		Commands = NewObject<URPGItemCommandComponent>(Controller);
		Controller->AddInstanceComponent(Commands);
		Commands->RegisterComponent();
		Grant = Pickup->FindComponentByClass<URPGPickupGrantComponent>();
		// This isolated world does not BeginPlay its actors. Give the grant its
		// real component lifecycle so an uncertain pending grant can EndPlay.
		if (Grant)
		{
			Grant->RegisterAllComponentTickFunctions(true);
			Grant->BeginPlay();
		}
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 31, 3, {}, {}, State);
		FRPGItemLocation Location;
		Location.ContainerType = ERPGItemContainerType::Inventory;
		Location.ContainerId = Request.Actor.OwnerId;
		Location.SlotIndex = 0;
		FRPGItemRecord Record;
		FRPGItemRecordMetadata Metadata;
		Metadata.CreationSource = TEXT("WorldPickup");
		FRPGItemRecord::TryCreate(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("RuntimePickup")),
			1, Request.Actor, Location, State, Metadata, Record);
		FRPGItemRecordMutation& Mutation = Request.Mutations.AddDefaulted_GetRef();
		Mutation.NewRecord = MoveTemp(Record);
	}
	~FFixture()
	{
		if (Grant && Grant->HasBegunPlay()) Grant->EndPlay(EEndPlayReason::Destroyed);
		if (World)
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
	bool Start(bool bPrepared = false) { return Grant && Commands && Grant->BeginGrant(Request, *Commands, Committer, bPrepared); }
	bool Plan(const URPGItemDefinition& Definition, const IRPGItemDefinitionCatalog& Definitions,
		FRPGItemRepositoryCommitRequest& OutCommit, const bool bWithServerReservations = true) const
	{
		FRPGItemPickupGrantRequest Next;
		Next.RequestId = FGuid::NewGuid(); Next.ItemId = FGuid::NewGuid(); Next.Owner = Request.Actor;
		Next.Quantity = 3; Next.GenerationSeed = 31; Next.CreationSource = TEXT("WorldPickup");
		const auto* Records = Projection ? Projection->GetAuthoritativeRecordSource() : nullptr;
		return Records && (bWithServerReservations
			? FRPGPickupSlotReservationTestAccess::Plan(*Commands, Next, Definition, *Records, Definitions, OutCommit)
			: FRPGItemPickupGrantPlanner::TryPlan(Next, Definition, *Records, Definitions, OutCommit));
	}
};

URPGItemDefinition* MakeDefinition()
{
	auto* Definition = NewObject<URPGItemDefinition>();
	Definition->ItemTag = RPGGameplayTags::GameItem_Consume_Potion_Red_Large;
	Definition->ItemCategory = EItemCategory::Consume;
	Definition->DisplayName = FText::FromString(TEXT("Concurrent pickup test"));
	Definition->MaxStackSize = 10;
	Definition->DefinitionVersion = 1;
	return Definition;
}

FRPGItemRecord MakeStoredRecord(const FFixture& F, const URPGItemDefinition& Definition, int32 Slot)
{
	FRPGItemInstanceState State;
	Definition.BuildInstanceState(3, 17, State);
	FRPGItemLocation Location;
	Location.ContainerType = ERPGItemContainerType::Inventory; Location.ContainerId = F.Request.Actor.OwnerId; Location.SlotIndex = Slot;
	FRPGItemRecordMetadata Metadata; Metadata.CreationSource = TEXT("SlotInteractionFixture");
	FRPGItemRecord Record;
	FRPGItemRecord::TryCreate(Definition.GetPrimaryAssetId(), Definition.GetDefinitionVersion(), F.Request.Actor, Location, State, Metadata, Record);
	return Record.CopyWithRevision(1);
}

class FProjectionSink final : public IRPGItemCommitSink
{
public:
	explicit FProjectionSink(URPGItemCommandComponent& InCommands) : Commands(InCommands) {}
	virtual bool ApplyCommit(const FRPGItemOwnerRef& Owner, const FRPGItemBackendCommitResult& Result, FString& Error) override
	{ return Commands.ApplyCommitAndReconcile(Owner, Result, Error); }
	URPGItemCommandComponent& Commands;
};
class FNoEffects final : public IRPGItemFirstCommitSink
{
public:
	virtual bool EnqueueFirstCommit(const FRPGItemBackendCommitResult&, FString&) override { return true; }
};

struct FInventoryPipeline
{
	FFixture& F;
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	FProjectionSink Sink;
	FNoEffects Effects;
	FDeferredCommitter* Backend = nullptr;
	TUniquePtr<IRPGItemAsyncCommitter> Guard;
	TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> Pipeline;
	FRPGItemAsyncCommandOutcome Last;
	explicit FInventoryPipeline(FFixture& In, const URPGItemDefinition& Definition) : F(In), Sink(*In.Commands)
	{
		Definitions.RegisterDefinition(Definition);
		auto Fake = MakeUnique<FDeferredCommitter>(); Backend = Fake.Get();
		Guard = FRPGPickupSlotReservationTestAccess::Wrap(*F.Commands, MoveTemp(Fake));
		Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(
			*F.Projection->GetAuthoritativeRecordSource(), Definitions, Actions, *Guard, Sink, Effects);
		FRPGPickupSlotReservationTestAccess::Bind(*F.Commands, Pipeline);
	}
	~FInventoryPipeline() { FRPGPickupSlotReservationTestAccess::Bind(*F.Commands, {}); }
	FRPGItemAsyncCommandCompletion Done(FGuid Request)
	{
		return [this, Request](FRPGItemAsyncCommandOutcome Outcome)
		{
			Last = Outcome;
			FRPGPickupSlotReservationTestAccess::Finish(*F.Commands, Request, TEXT("Item.Move"), MoveTemp(Outcome));
		};
	}
	bool Move(const FRPGItemRecord& Source, int32 Slot, FGuid Request)
	{
		FRPGItemMoveRequest Move;
		Move.RequestId = Request; Move.Actor = F.Request.Actor; Move.ItemId = Source.GetItemId(); Move.ExpectedRevision = Source.GetRevision();
		Move.Destination.ContainerType = ERPGItemContainerType::Inventory; Move.Destination.ContainerId = Move.Actor.OwnerId; Move.Destination.SlotIndex = Slot;
		return Pipeline->Move(Move, Done(Request));
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupGrantRetryTest,
	"ProjectRPG.Item.PickupGrant.RuntimeRetriesFrozenCommitThenProjects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupGrantRetryTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupGrantRuntimeTests;
	TGuardValue<uint64> FrameCounterGuard(GFrameCounter, GFrameCounter);
	FFixture Fixture;
	if (Fixture.World) Fixture.World->GetTimerManager().Tick(0.f);
	if (!TestTrue(TEXT("Pickup grant starts"), Fixture.Start())) return false;
	TestEqual(TEXT("Owner sees one live pickup grant"), Fixture.Commands->GetPendingPickupGrantCount(), 1);
	TestFalse(TEXT("Claimed pickup is unavailable"), Fixture.Pickup->IsAvailableForPickup());
	TestFalse(TEXT("Concurrent grant is rejected"), Fixture.Start());
	TestEqual(TEXT("Rejected duplicate cannot increment presentation count"), Fixture.Commands->GetPendingPickupGrantCount(), 1);
	Fixture.Pickup->ReleasePickupClaim();
	Fixture.Pickup->PickedUp();
	TestFalse(TEXT("Pending grant cannot be released externally"), Fixture.Pickup->IsAvailableForPickup());
	TestFalse(TEXT("Pickup cannot retire before a receipt"), Fixture.Pickup->IsActorBeingDestroyed());
	Fixture.Committer->Reply(ERPGItemBackendStatus::TransportError);
	TestEqual(TEXT("Lost response preserves pending presentation"), Fixture.Commands->GetPendingPickupGrantCount(), 1);
	TestTrue(TEXT("Lost response keeps the pickup claimed"), Fixture.Grant->IsPending());
	++GFrameCounter;
	Fixture.World->GetTimerManager().Tick(3.f);
	if (!TestEqual(TEXT("Timer retries the grant"), Fixture.Committer->Requests.Num(), 2)) return false;
	FString FirstBody, RetryBody;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Fixture.Committer->Requests[0], FirstBody);
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Fixture.Committer->Requests[1], RetryBody);
	TestEqual(TEXT("Retry sends the exact frozen command"), FirstBody, RetryBody);
	Fixture.Committer->Reply(ERPGItemBackendStatus::Unauthorized);
	TestTrue(TEXT("Authentication failure cannot disprove an earlier lost commit"), Fixture.Grant->IsPending());
	TestFalse(TEXT("Authentication failure does not expose the pickup to another player"),
		Fixture.Pickup->IsAvailableForPickup());
	++GFrameCounter;
	Fixture.World->GetTimerManager().Tick(5.f);
	if (!TestEqual(TEXT("Authentication recovery retries the frozen grant"), Fixture.Committer->Requests.Num(), 3)) return false;
	Fixture.Committer->Reply(ERPGItemBackendStatus::Succeeded, true);
	TestTrue(TEXT("Malformed success does not retire the pickup"), Fixture.Grant->IsPending());
	++GFrameCounter;
	Fixture.World->GetTimerManager().Tick(9.f);
	if (!TestEqual(TEXT("Malformed receipt is retried safely"), Fixture.Committer->Requests.Num(), 4)) return false;
	Fixture.Committer->Reply(ERPGItemBackendStatus::AlreadyApplied);
	FRPGInventoryProjectionEntry Entry;
	TestTrue(TEXT("Receipt appears in the owner inventory"), Fixture.Projection->FindProjectedItem(
		Fixture.Request.Mutations[0].NewRecord.GetItemId(), Entry));
	TestEqual(TEXT("Projection preserves the pickup quantity"), Entry.GetQuantity(), 3);
	TestTrue(TEXT("Persisted pickup retires after projection"), Fixture.Pickup->IsActorBeingDestroyed());
	TestFalse(TEXT("Completed grant stops retrying"), Fixture.Grant->IsPending());
	TestEqual(TEXT("Only completed grant clears presentation"), Fixture.Commands->GetPendingPickupGrantCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupGrantRejectionTest,
	"ProjectRPG.Item.PickupGrant.RuntimeConflictReleasesPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPreparedPickupRuntimeTest,
	"ProjectRPG.Item.PickupGrant.RuntimePreparedConflictPreservesStableDropAndFrozenRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPreparedPickupRuntimeTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	TGuardValue<uint64> FrameCounterGuard(GFrameCounter, GFrameCounter);
	FFixture F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	F.World->GetTimerManager().Tick(0.f);
	// This fixture deliberately has no world BeginPlay/actor initialization.
	// Route the real post-init hook rather than inventing a drop GUID in the test.
	if (!F.Pickup->IsActorInitialized()) F.Pickup->PostInitializeComponents();
	const FGuid Initial = F.Pickup->GetPickupDropId();
	TestTrue(TEXT("Spawned authority owns a valid drop id"), Initial.IsValid());
	F.Pickup->InitItemManifest(F.Pickup->GetItemManifest());
	const FGuid Fresh = F.Pickup->GetPickupDropId();
	TestTrue(TEXT("Fresh/pool initialization is a different drop"), Fresh.IsValid() && Fresh != Initial);
	if (!TestTrue(TEXT("Prepared grant starts"), F.Start(true))) return false;
	F.Committer->Reply(ERPGItemBackendStatus::LocationConflict);
	TestTrue(TEXT("Known conflict keeps pending reservation"), F.Grant->IsPending());
	TestEqual(TEXT("Prepared conflict remains visible as a pending pickup"), F.Commands->GetPendingPickupGrantCount(), 1);
	TestFalse(TEXT("Another claimant cannot take the drop"), F.Pickup->IsAvailableForPickup());
	F.Pickup->InitItemManifest(F.Pickup->GetItemManifest()); F.Pickup->ReleasePickupClaim();
	TestEqual(TEXT("Pending initialization cannot regenerate drop identity"), F.Pickup->GetPickupDropId(), Fresh);
	++GFrameCounter; F.World->GetTimerManager().Tick(3.f);
	if (!TestEqual(TEXT("Same original grant retries"), F.Committer->Requests.Num(), 2)) return false;
	FString Original, Retry;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Committer->Requests[0], Original);
	FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Committer->Requests[1], Retry);
	TestEqual(TEXT("Every original field retained"), Original, Retry);
	F.Committer->Reply(ERPGItemBackendStatus::Succeeded);
	TestTrue(TEXT("Matching persisted outcome retires world pickup"), F.Pickup->IsActorBeingDestroyed());
	return true;
}

// A revoked claimant must not keep writing, or release a pickup whose original
// commit is uncertain. A success already in flight still retires the world pickup.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupGrantRevocationTest,
	"ProjectRPG.Item.PickupGrant.RuntimeRevocationStopsRetriesWithoutReleasingUnknownClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupGrantRevocationTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	TGuardValue<uint64> FrameCounterGuard(GFrameCounter, GFrameCounter);
	{
		FFixture F;
		if (!TestNotNull(TEXT("World"), F.World)) return false;
		F.World->GetTimerManager().Tick(0.f);
		if (!TestTrue(TEXT("Frozen grant starts"), F.Start())) return false;
		F.Committer->Reply(ERPGItemBackendStatus::TransportError);
		F.PlayerState->SetOwner(F.Controller);
		TestTrue(TEXT("Exact claimant revoked"), F.PlayerState->RevokeAuthenticatedIdentity(
			F.PlayerState->GetBackendDungeonSessionId(), F.PlayerState->GetBackendCharacterId(),
			F.PlayerState->GetAuthenticatedSteamId()));
		TestEqual(TEXT("Revocation retires only the old owner's UI count"), F.Commands->GetPendingPickupGrantCount(), 0);
		++GFrameCounter; F.World->GetTimerManager().Tick(3.f);
		TestEqual(TEXT("No backend retry after revocation"), F.Committer->Requests.Num(), 1);
		TestTrue(TEXT("Unknown prior outcome remains pending"), F.Grant->IsPending());
		TestFalse(TEXT("Unknown prior outcome never exposes pickup to another claimant"), F.Pickup->IsAvailableForPickup());
		TestTrue(TEXT("Revoked projection remains empty"), F.Projection->GetProjectedItems().IsEmpty());
	}
	{
		FFixture F;
		if (!TestTrue(TEXT("In-flight grant starts"), F.Start())) return false;
		F.PlayerState->SetOwner(F.Controller);
		F.PlayerState->RevokeAuthenticatedIdentity(F.PlayerState->GetBackendDungeonSessionId(),
			F.PlayerState->GetBackendCharacterId(), F.PlayerState->GetAuthenticatedSteamId());
		F.Committer->Reply(ERPGItemBackendStatus::Succeeded);
		TestFalse(TEXT("Known committed outcome ends pending grant"), F.Grant->IsPending());
		TestTrue(TEXT("Known committed pickup retires without a duplicate grant"), F.Pickup->IsActorBeingDestroyed());
		TestTrue(TEXT("Late success cannot restore revoked projection"), F.Projection->GetProjectedItems().IsEmpty());
	}
	return true;
}

bool FRPGPickupGrantRejectionTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture Fixture;
	if (!TestTrue(TEXT("Pickup grant starts"), Fixture.Start())) return false;
	Fixture.Committer->Reply(ERPGItemBackendStatus::LocationConflict);
	TestTrue(TEXT("Definitive rejection releases the pickup"), Fixture.Pickup->IsAvailableForPickup());
	TestFalse(TEXT("Rejected pickup is not destroyed"), Fixture.Pickup->IsActorBeingDestroyed());
	TestFalse(TEXT("Rejected grant leaves no pending work"), Fixture.Grant->IsPending());
	TestEqual(TEXT("Definitive unprepared rejection clears UI count"), Fixture.Commands->GetPendingPickupGrantCount(), 0);
	TestEqual(TEXT("Rejected grant adds no inventory entries"), Fixture.Projection->GetProjectedItems().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupGrantCharacterSwitchTest,
	"ProjectRPG.Item.PickupGrant.RuntimeCharacterSwitchDoesNotMisapplyReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupGrantCharacterSwitchTest::RunTest(const FString& Parameters)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture Fixture;
	if (!TestTrue(TEXT("Pickup grant starts"), Fixture.Start())) return false;
	Fixture.PlayerState->SetAuthenticatedIdentity(
		TEXT("10203040-5060-7080-90a0-b0c0d0e0f002"),
		TEXT("76561198000000001"), TEXT("pickup-test-session"));
	Fixture.Committer->Reply(ERPGItemBackendStatus::Succeeded);
	TestEqual(TEXT("Old character receipt is not applied to the switched controller"),
		Fixture.Projection->GetProjectedItems().Num(), 0);
	TestTrue(TEXT("Durably granted pickup still retires"), Fixture.Pickup->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupGrantPresentationTest,
	"ProjectRPG.Item.PickupGrant.RuntimePendingPresentationSurvivesRebindWithoutReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupGrantPresentationTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	TGuardValue<uint64> FrameCounterGuard(GFrameCounter, GFrameCounter);
	FFixture F;
	if (!TestNotNull(TEXT("Controller"), F.Controller)) return false;
	F.World->GetTimerManager().Tick(0.f);
	// Applying isolated records does not finish the real asynchronous load. Match
	// the existing presentation fixture's Ready setup without weakening that guard.
	auto* LoadState = FindFProperty<FEnumProperty>(F.Projection->GetClass(), TEXT("LoadState"));
	if (!TestNotNull(TEXT("Projection load state"), LoadState) || !LoadState->GetUnderlyingProperty()) return false;
	LoadState->GetUnderlyingProperty()->SetIntPropertyValue(LoadState->ContainerPtrToValuePtr<void>(F.Projection),
		static_cast<uint64>(ERPGInventoryProjectionLoadState::Ready));
	auto* Model = NewObject<URPGInventoryProjectionViewModel>(F.Controller);
	Model->InitializeFromPlayerController(F.Controller);
	TestEqual(TEXT("Initially no pending pickup"), Model->PendingItemCommandCount, 0);
	if (!TestTrue(TEXT("Real grant lifecycle starts"), F.Start(true))) return false;
	TestEqual(TEXT("Shared UI observes the server-reported count"), Model->PendingItemCommandCount, 1);
	const FText Expected = NSLOCTEXT("RPGItemFeedback", "PickupPending",
		"Confirming an item pickup. Do not repeat the same pickup.");
	TestTrue(TEXT("Existing feedback path presents pickup confirmation"), Model->GetCommandFeedbackText().EqualTo(Expected));
	TestFalse(TEXT("Pickup is not a manually replayable inventory command"), Model->bHasInventoryRecovery);
	TestFalse(TEXT("Pickup alone does not disable unrelated item changes"), Model->GetInventoryCapacity().bItemChangesBlocked);
	F.Committer->Reply(ERPGItemBackendStatus::TransportError);
	Model->InitializeFromPlayerController(nullptr);
	TestEqual(TEXT("Unbound view drops presentation only"), Model->PendingItemCommandCount, 0);
	Model->InitializeFromPlayerController(F.Controller);
	TestEqual(TEXT("Rebinding reads retained controller state"), Model->PendingItemCommandCount, 1);
	TestTrue(TEXT("Rebinding restores feedback without receiving another event"), Model->GetCommandFeedbackText().EqualTo(Expected));
	TestEqual(TEXT("View rebind never sends another grant"), F.Committer->Requests.Num(), 1);
	TestFalse(TEXT("No fake recovery button for a pickup"), Model->bCanRetryInventoryRecovery);
	++GFrameCounter;
	F.World->GetTimerManager().Tick(3.f); // Existing grant retry, not initiated by the view.
	if (!TestEqual(TEXT("Only the existing grant timer retries"), F.Committer->Requests.Num(), 2)) return false;
	F.Committer->Reply(ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Matching receipt clears the shared UI count"), Model->PendingItemCommandCount, 0);
	TestTrue(TEXT("Only the real receipt populates the view"), Model->InventoryItems.ContainsByPredicate(
		[&F](const auto& Entry) { return Entry.GetItemId() == F.Request.Mutations[0].NewRecord.GetItemId(); }));
	TestFalse(TEXT("Completed pickup notice is no longer displayed"), Model->GetCommandFeedbackText().EqualTo(Expected));
	Model->InitializeFromPlayerController(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConcurrentPickupSlotsTest,
	"ProjectRPG.Item.PickupGrant.RuntimeConcurrentPickupsReserveDistinctDestinations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGConcurrentPickupSlotsTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	TGuardValue<uint64> FrameCounterGuard(GFrameCounter, GFrameCounter);
	FFixture F;
	if (!TestNotNull(TEXT("World"), F.World)) return false;
	F.World->GetTimerManager().Tick(0.f);
	auto* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRPGItemRepositoryCommitRequest First, Second, Collision, Next;
	if (!TestTrue(TEXT("First plan"), F.Plan(*Definition, Definitions, First))) return false;
	F.Request = First;
	if (!TestTrue(TEXT("First pending grant"), F.Start(true))) return false;
	if (!TestTrue(TEXT("Second plan while first HTTP request is pending"), F.Plan(*Definition, Definitions, Second))) return false;
	TestEqual(TEXT("First request keeps slot zero"), First.Mutations[0].NewRecord.GetLocation().SlotIndex, 0);
	TestEqual(TEXT("Second request uses a different empty slot"), Second.Mutations[0].NewRecord.GetLocation().SlotIndex, 1);
	auto* SecondPickup = F.World->SpawnActor<ARPGPickUpBase>();
	auto* SecondGrant = SecondPickup ? SecondPickup->FindComponentByClass<URPGPickupGrantComponent>() : nullptr;
	auto SecondCommitter = MakeShared<FDeferredCommitter>();
	if (!TestTrue(TEXT("Second grant starts without waiting for first response"), SecondGrant &&
		SecondGrant->BeginGrant(Second, *F.Commands, SecondCommitter, true))) return false;
	TestEqual(TEXT("Both requests remain visible"), F.Commands->GetPendingPickupGrantCount(), 2);
	if (!TestTrue(TEXT("Unfiltered caller can reproduce original same-slot bug"), F.Plan(*Definition, Definitions, Collision, false))) return false;
	TestEqual(TEXT("Old planner inputs still see pending slot zero as empty"), Collision.Mutations[0].NewRecord.GetLocation().SlotIndex, 0);
	auto* RejectedPickup = F.World->SpawnActor<ARPGPickUpBase>();
	auto* RejectedGrant = RejectedPickup ? RejectedPickup->FindComponentByClass<URPGPickupGrantComponent>() : nullptr;
	auto RejectedCommitter = MakeShared<FDeferredCommitter>();
	if (!TestNotNull(TEXT("Rejected grant"), RejectedGrant)) return false;
	TestFalse(TEXT("Reservation check also guards callers that omit exclusions"),
		RejectedGrant->BeginGrant(Collision, *F.Commands, RejectedCommitter, true));
	TestEqual(TEXT("Rejected reservation sends no HTTP command"), RejectedCommitter->Requests.Num(), 0);
	TestTrue(TEXT("Only the never-sent pickup can be claimed again"), RejectedPickup->IsAvailableForPickup());
	TestEqual(TEXT("Rejected reservation cannot clear another grant"), F.Commands->GetPendingPickupGrantCount(), 2);
	SecondCommitter->Reply(ERPGItemBackendStatus::AlreadyApplied); // Responses may finish out of order.
	TestEqual(TEXT("Only completed second request is retired"), F.Commands->GetPendingPickupGrantCount(), 1);
	F.Committer->Reply(ERPGItemBackendStatus::TransportError);
	if (!TestTrue(TEXT("Third plan during first response loss"), F.Plan(*Definition, Definitions, Next))) return false;
	TestEqual(TEXT("Occupied second slot and unresolved first slot are both excluded"), Next.Mutations[0].NewRecord.GetLocation().SlotIndex, 2);
	++GFrameCounter; F.World->GetTimerManager().Tick(3.f);
	if (!TestEqual(TEXT("Original grant retries"), F.Committer->Requests.Num(), 2)) return false;
	FString OriginalBody, RetryBody;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Committer->Requests[0], OriginalBody);
	FRPGItemBackendJsonCodec::SerializeCommitRequest(F.Committer->Requests[1], RetryBody);
	TestEqual(TEXT("Original pending request is never moved to a new slot"), OriginalBody, RetryBody);
	F.Committer->Reply(ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Two genuine receipts produce two items"), F.Projection->GetProjectedItems().Num(), 2);
	TestEqual(TEXT("All known successful requests are retired"), F.Commands->GetPendingPickupGrantCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupUnknownTeardownSlotTest,
	"ProjectRPG.Item.PickupGrant.RuntimeUnknownTeardownKeepsOriginalDestinationReserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPickupUnknownTeardownSlotTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture F;
	if (!TestTrue(TEXT("Original prepared grant starts"), F.Start(true))) return false;
	F.Committer->Reply(ERPGItemBackendStatus::TransportError);
	F.Grant->EndPlay(EEndPlayReason::Destroyed);
	TestEqual(TEXT("Actor teardown is not proof that the original slot is unused"), F.Commands->GetPendingPickupGrantCount(), 1);
	auto* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions;
	Definitions.RegisterDefinition(*Definition);
	FRPGItemRepositoryCommitRequest Next;
	if (!TestTrue(TEXT("Another drop can still use another slot"), F.Plan(*Definition, Definitions, Next))) return false;
	TestEqual(TEXT("Original uncertain destination stays excluded"), Next.Mutations[0].NewRecord.GetLocation().SlotIndex, 1);
	TestTrue(TEXT("No synthetic item appears from a reservation"), F.Projection->GetProjectedItems().IsEmpty());
	TestEqual(TEXT("No automatic new grant from teardown"), F.Committer->Requests.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPickupCrossSlotUiTest,
	"ProjectRPG.Item.PickupGrant.RuntimePickupReservationBlocksBagDestinationAndUi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPickupCrossSlotUiTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture F;
	auto* Definition = MakeDefinition();
	const auto Source = MakeStoredRecord(F, *Definition, 2);
	if (!TestTrue(TEXT("Stored source snapshot"), F.Projection->ApplyAuthoritativeRecords(F.Request.Actor, {Source}))) return false;
	auto* Ready = FindFProperty<FEnumProperty>(F.Projection->GetClass(), TEXT("LoadState"));
	if (!TestNotNull(TEXT("Load state"), Ready)) return false;
	Ready->GetUnderlyingProperty()->SetIntPropertyValue(Ready->ContainerPtrToValuePtr<void>(F.Projection), static_cast<uint64>(ERPGInventoryProjectionLoadState::Ready));
	auto* Model = NewObject<URPGInventoryProjectionViewModel>(F.Controller);
	Model->InitializeFromPlayerController(F.Controller);
	if (!TestTrue(TEXT("Pickup starts into slot zero"), F.Start(true))) return false;
	TestTrue(TEXT("Owner presentation contains pickup slot"), F.Commands->IsInventorySlotReservedByPickup(0));
	TestEqual(TEXT("Empty cell is pending rather than free"), Model->GetItemSlotPresentationState({}, ERPGItemContainerType::Inventory, 0), ERPGItemSlotPresentationState::Pending);
	TestEqual(TEXT("Capacity counts one reserved empty slot"), Model->GetInventoryCapacity().ReservedEmptySlots, 1);
	TestFalse(TEXT("Bag move cannot choose pickup destination"), Model->CanMoveItemToSlot(Source.GetItemId(), 1, 0));
	TestFalse(TEXT("Split cannot choose pickup destination"), Model->CanSplitItem(Source.GetItemId(), 1, 0, 1));
	TestTrue(TEXT("Other empty slots remain usable"), Model->CanMoveItemToSlot(Source.GetItemId(), 1, 1));
	FInventoryPipeline Inventory(F, *Definition);
	TestFalse(TEXT("Direct mutation path also refuses the occupied reservation"), Inventory.Move(Source, 0, FGuid::NewGuid()));
	TestEqual(TEXT("Local conflict sends no backend command"), Inventory.Backend->Requests.Num(), 0);
	TestEqual(TEXT("Local refusal is not reported as a PostgreSQL conflict"), Inventory.Last.BackendResult.Status, ERPGItemBackendStatus::ValidationFailed);
	TestFalse(TEXT("Never-sent move is not falsely uncertain"), Inventory.Pipeline->HasUncertainInventoryCommands());
	TestTrue(TEXT("No manual inventory recovery for the pickup"), F.Commands->InventoryRecovery.IsEmpty());
	Model->InitializeFromPlayerController(nullptr); Model->InitializeFromPlayerController(F.Controller);
	TestEqual(TEXT("Rebind restores reserved cell"), Model->GetInventoryCapacity().ReservedEmptySlots, 1);
	F.Committer->Reply(ERPGItemBackendStatus::AlreadyApplied);
	TestFalse(TEXT("Genuine completion releases pickup reservation"), F.Commands->IsInventorySlotReservedByPickup(0));
	TestEqual(TEXT("Pending capacity is not retained after projection"), Model->GetInventoryCapacity().ReservedEmptySlots, 0);
	TestEqual(TEXT("New pickup item occupies its real slot"), Model->GetInventoryCapacity().OccupiedSlots, 2);
	Model->InitializeFromPlayerController(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryBeforePickupSlotTest,
	"ProjectRPG.Item.PickupGrant.RuntimeBagDestinationSurvivesUncertainReplayAndCachedCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryBeforePickupSlotTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture F;
	auto* Definition = MakeDefinition();
	const auto Source = MakeStoredRecord(F, *Definition, 2);
	if (!TestTrue(TEXT("Stored source snapshot"), F.Projection->ApplyAuthoritativeRecords(F.Request.Actor, {Source}))) return false;
	FInventoryPipeline Inventory(F, *Definition);
	const FGuid Request = FGuid::NewGuid();
	if (!TestTrue(TEXT("Bag move starts before pickup"), Inventory.Move(Source, 0, Request))) return false;
	TestEqual(TEXT("One server-tracked inventory request"), FRPGPickupSlotReservationTestAccess::InventoryRequests(*F.Commands), 1);
	FRPGItemRepositoryCommitRequest Next;
	if (!TestTrue(TEXT("New pickup plans around pending move"), F.Plan(*Definition, Inventory.Definitions, Next))) return false;
	TestEqual(TEXT("Move destination excluded from pickup plan"), Next.Mutations[0].NewRecord.GetLocation().SlotIndex, 1);
	F.Request = Next;
	if (!TestTrue(TEXT("Non-conflicting pickup proceeds concurrently"), F.Start(true))) return false;
	Inventory.Backend->Reply(ERPGItemBackendStatus::TransportError);
	TestTrue(TEXT("Original move becomes uncertain"), Inventory.Pipeline->HasUncertainInventoryCommands());
	TestEqual(TEXT("Unknown outcome retains original slot reservation"), FRPGPickupSlotReservationTestAccess::InventoryRequests(*F.Commands), 1);
	F.Committer->Reply(ERPGItemBackendStatus::AlreadyApplied);
	if (!TestTrue(TEXT("Original move uses frozen manual recovery"), Inventory.Pipeline->RetryInventoryCommand(Request, F.Request.Actor, Inventory.Done(Request)))) return false;
	TestEqual(TEXT("Only the same move is retried"), Inventory.Backend->Requests.Num(), 2);
	FString Before, Retry;
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Inventory.Backend->Requests[0], Before);
	FRPGItemBackendJsonCodec::SerializeCommitRequest(Inventory.Backend->Requests[1], Retry);
	TestEqual(TEXT("Frozen retry remains byte-identical"), Before, Retry);
	Inventory.Backend->Reply(ERPGItemBackendStatus::AlreadyApplied);
	TestEqual(TEXT("Verified completion releases only the move reservation"), FRPGPickupSlotReservationTestAccess::InventoryRequests(*F.Commands), 0);
	TestFalse(TEXT("Recovery resolved"), Inventory.Pipeline->HasUncertainInventoryCommands());
	TestTrue(TEXT("Cached old request still succeeds without re-planning"), Inventory.Move(Source, 0, Request));
	TestEqual(TEXT("Cached completion sends no third command"), Inventory.Backend->Requests.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGFrozenSlotRequestIdentityTest,
	"ProjectRPG.Item.PickupGrant.RuntimeSlotGuardCannotReplaceFrozenRequestOrOtherReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGFrozenSlotRequestIdentityTest::RunTest(const FString&)
{
	using namespace RPGPickupGrantRuntimeTests;
	FFixture F;
	auto* Definition = MakeDefinition();
	FRPGItemDefinitionRegistry Definitions; Definitions.RegisterDefinition(*Definition);
	FRPGItemRepositoryCommitRequest Frozen, Changed, Next;
	if (!TestTrue(TEXT("Construct a native commit request"), F.Plan(*Definition, Definitions, Frozen))) return false;
	Frozen.Operation = TEXT("Item.ClaimMail"); // The shared guard validates mutation destinations, not RPC names.
	if (!TestTrue(TEXT("Track inventory destination"), FRPGPickupSlotReservationTestAccess::Track(*F.Commands, Frozen))) return false;
	Changed = Frozen; Changed.CommandFingerprint += TEXT(".changed");
	TestFalse(TEXT("Same id cannot replace the original frozen body"), FRPGPickupSlotReservationTestAccess::Track(*F.Commands, Changed));
	TestTrue(TEXT("Exact original remains retryable"), FRPGPickupSlotReservationTestAccess::Track(*F.Commands, Frozen));
	TestEqual(TEXT("No replacement or extra request added"), FRPGPickupSlotReservationTestAccess::InventoryRequests(*F.Commands), 1);
	if (!TestTrue(TEXT("Pickup plans around shared mutation guard"), F.Plan(*Definition, Definitions, Next))) return false;
	TestEqual(TEXT("Shared mutation target remains excluded"), Next.Mutations[0].NewRecord.GetLocation().SlotIndex, 1);
	TestFalse(TEXT("A raw pickup cannot bypass the same guard"), F.Start(true));
	TestTrue(TEXT("Never-sent pickup claim is restored"), F.Pickup->IsAvailableForPickup());
	TestEqual(TEXT("No pickup backend call"), F.Committer->Requests.Num(), 0);
	TestEqual(TEXT("Another request's reservation survives rejection"), FRPGPickupSlotReservationTestAccess::InventoryRequests(*F.Commands), 1);
	return true;
}

#endif
