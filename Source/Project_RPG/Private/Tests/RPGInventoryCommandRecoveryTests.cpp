#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Policy/RPGItemInventoryPolicy.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Transaction/RPGItemAsyncCommandOrchestrator.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"
#include "Player/RPGPlayerState.h"
#include "UObject/Package.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

struct FRPGInventoryRecoveryRevocationTestAccess
{
	static void Attach(URPGItemCommandComponent& Commands,
		const TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>& Pipeline)
	{ Commands.Orchestrator = Pipeline; }
	static void Finish(URPGItemCommandComponent& Commands, FGuid Request, FRPGItemAsyncCommandOutcome Outcome)
	{ Commands.HandleOutcome(Request, TEXT("Item.Move"), MoveTemp(Outcome)); }
	static void ReplicateRevocation(URPGItemCommandComponent& Commands)
	{ Commands.bAuthenticatedIdentityRevoked = true; Commands.OnRep_InventoryRecovery(); }
	static void ReplicateRecovery(URPGItemCommandComponent& Commands) { Commands.OnRep_InventoryRecovery(); }
	static void AttachJournal(URPGItemCommandComponent& Commands, TSharedPtr<FRPGInventoryCommandJournal> Journal)
	{ Commands.InventoryJournal = MoveTemp(Journal); }
	static void BlockStorage(URPGItemCommandComponent& Commands) { Commands.BlockInventoryRecoveryStorage(); }
};

namespace RPGInventoryCommandRecoveryTests
{
struct FFixture : IRPGItemAsyncCommitter, IRPGItemCommitSink, IRPGItemFirstCommitSink
{
	FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-2222-3333-4444-555555555555")};
	FPrimaryAssetId Definition{TEXT("RPGItemDefinition"), TEXT("RecoveryPotion")};
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Actions;
	FRPGInventoryProjectionStore Store;
	FRPGItemRecord Source, Other;
	TArray<FString> Bodies;
	FRPGItemBackendCommitResult Saved;
	ERPGItemBackendStatus Reply = ERPGItemBackendStatus::TransportError;
	bool bHold = false, bWrongReceipt = false, bWrongRecord = false, bWrongQuantity = false;
	bool bEarlierAttemptUncertain = false;
	FRPGItemBackendCommitCompletion Held;
	int32 Writes = 0, FirstEffects = 0;
	URPGItemCommandComponent* LiveCommands = nullptr;
	int32 ApplyCalls = 0;
	TFunction<void()> BeforeCommit;
	TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> Pipeline;
	FFixture()
	{
		Definitions.RegisterSnapshot({Definition, 1, 10});
		Source = Make(0, 7); Other = Make(1, 1);
		TArray<FRPGInventoryProjectionEntry> Entries; Store.Replace(Owner, {Source, Other}, Entries);
		Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(Store, Definitions, Actions, *this, *this, *this);
	}
	FRPGItemRecord Make(int32 Slot, int32 Quantity) const
	{
		FRPGItemInstanceState State;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 37, Quantity, {}, {}, State);
		FRPGItemRecord Result;
		FRPGItemRecord::TryRestore(Definition, 1, Owner, {ERPGItemContainerType::Inventory, Owner.OwnerId, Slot},
			State, 9007199254740993LL, ERPGItemLifecycleState::Active, {}, Result);
		return Result;
	}
	FRPGItemMoveRequest Move() const
	{
		FRPGItemMoveRequest R; R.RequestId = FGuid::NewGuid(); R.Actor = Owner;
		R.ItemId = Source.GetItemId(); R.ExpectedRevision = Source.GetRevision();
		R.Destination = {ERPGItemContainerType::Inventory, Owner.OwnerId, 3}; return R;
	}
	bool PrepareEquipment(const bool bUnequip)
	{
		Definition = {TEXT("RPGItemDefinition"), TEXT("RecoveryEquipment")};
		Definitions.RegisterSnapshot({Definition, 1, 1});
		FRPGItemActionPolicy Policy;
		Policy.DefinitionId = Definition; Policy.DefinitionVersion = 1;
		Policy.CompatibleEquipmentSlots.Add(EEquipmentSlotType::Head);
		if (!Actions.RegisterPolicy(Policy)) return false;
		Source = Make(0, 1);
		auto Metadata = Source.GetMetadata();
		Metadata.BindState = bUnequip ? ERPGItemBindState::CharacterBound : ERPGItemBindState::BindOnEquip;
		Metadata.Durability.Current = 17; Metadata.Durability.Maximum = 23;
		Metadata.CreationSource = TEXT("EquipmentRecovery.fixture");
		const FRPGItemLocation Location{bUnequip ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory,
			Owner.OwnerId, bUnequip ? static_cast<int32>(EEquipmentSlotType::Head) : 0};
		if (!FRPGItemRecord::TryRestore(Definition, 1, Owner, Location, Source.GetState(), Source.GetRevision(),
			ERPGItemLifecycleState::Active, Metadata, Source)) return false;
		TArray<FRPGInventoryProjectionEntry> Entries;
		return Store.Replace(Owner, {Source, Other}, Entries);
	}
	bool Commit(const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion) override
	{
		if (BeforeCommit) BeforeCommit();
		FString Body; FRPGItemBackendJsonCodec::SerializeCommitRequest(Request, Body); Bodies.Add(Body);
		if (Writes == 0)
		{
			++Writes; Saved.RequestId = Request.RequestId; Saved.Operation = Request.Operation;
			Saved.Actor = Request.Actor; Saved.CommandFingerprint = Request.CommandFingerprint; Saved.AffectedQuantity = Request.AffectedQuantity;
			for (const auto& M : Request.Mutations) Saved.Records.Add(M.NewRecord.CopyWithRevision(M.ExpectedRevision + 1));
		}
		auto Result = Saved; Result.Status = Reply;
		Result.bCommitOutcomeUncertain = bEarlierAttemptUncertain;
		if (bWrongReceipt) Result.RequestId = FGuid::NewGuid();
		if (bWrongRecord) Result.Records[0].TryMoveTo({ERPGItemContainerType::Inventory, Owner.OwnerId, 5}, Result.Records[0]);
		if (bWrongQuantity) ++Result.AffectedQuantity;
		if (bHold) Held = MoveTemp(Completion);
		else Completion(MoveTemp(Result));
		return true;
	}
	bool ApplyCommit(const FRPGItemOwnerRef& ExpectedOwner, const FRPGItemBackendCommitResult& Result, FString& Error) override
	{
		++ApplyCalls;
		if (LiveCommands) return LiveCommands->ApplyCommitAndReconcile(ExpectedOwner, Result, Error);
		TArray<FRPGInventoryProjectionEntry> Entries;
		return ExpectedOwner == Owner && Store.ApplyMutations(Owner, Result.Records, Entries, &Error);
	}
	bool EnqueueFirstCommit(const FRPGItemBackendCommitResult& Result, FString& Error) override
	{
		if (LiveCommands) return LiveCommands->EnqueueFirstCommitEffect(Result, Error);
		++FirstEffects; return true;
	}
};

struct FUIFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	APlayerController* PC = nullptr;
	ARPGPlayerState* State = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGItemCommandComponent* Commands = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	const FString Session = TEXT("11111111-2222-3333-4444-555555555556");
	const FString Steam = TEXT("76561198000000001");
	explicit FUIFixture(const FFixture& F)
	{
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		PC = World->SpawnActor<APlayerController>(); State = World->SpawnActor<ARPGPlayerState>();
		if (!PC || !State) return;
		PC->SetPlayerState(State); State->SetOwner(PC);
		State->SetAuthenticatedIdentity(F.Owner.OwnerId, Steam, Session);
		Projection = NewObject<URPGInventoryProjectionComponent>(PC); PC->AddInstanceComponent(Projection); Projection->RegisterComponent();
		Commands = NewObject<URPGItemCommandComponent>(PC); PC->AddInstanceComponent(Commands); Commands->RegisterComponent();
		Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, F.Other});
		Projection->ApplyCompletedRewardSnapshot(F.Owner, {F.Source, F.Other}, Projection->GetAuthoritativeSnapshotGeneration());
		Model = NewObject<URPGInventoryProjectionViewModel>(PC); Model->InitializeFromPlayerController(PC);
	}
	~FUIFixture()
	{
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
};

struct FJournalFiles
{
	FString Base = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests/InventoryCommandJournal")));
	FString Leaf = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FString Directory = FPaths::Combine(Base, Leaf);
	FRPGInventoryJournalScope Scope{TEXT("http://127.0.0.1:3000/api"), TEXT("journal-test-server"),
		TEXT("11111111-2222-3333-4444-555555555556"), TEXT("11111111-2222-3333-4444-555555555555"), TEXT("76561198000000001")};
	TSharedPtr<FRPGInventoryCommandJournal> Open() const
	{ return MakeShared<FRPGInventoryCommandJournal>(Directory, Scope); }
	~FJournalFiles()
	{
		FGuid Id;
		if (FGuid::ParseExact(Leaf, EGuidFormats::Digits, Id) && Id.IsValid() && Directory == FPaths::Combine(Base, Leaf))
			IFileManager::Get().DeleteDirectory(*Directory, false, true); // This fixture's unique test directory only.
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryCommandRecoveryFrozenTest,
	"ProjectRPG.Item.InventoryRecovery.FrozenPlanAfterUnknownCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryCommandRecoveryFrozenTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F; auto R = F.Move(); FRPGItemAsyncCommandOutcome Outcome;
	auto Done = [&Outcome](FRPGItemAsyncCommandOutcome O) { Outcome = MoveTemp(O); };
	F.Pipeline->Move(R, Done);
	TestEqual(TEXT("Lost result is not a final failure"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
	TestTrue(TEXT("Original command retained"), F.Pipeline->HasUncertainInventoryCommands());
	FRPGItemRecord Before; F.Store.Find(F.Source.GetItemId(), Before);
	TestEqual(TEXT("No optimistic projection"), Before.GetRevision(), R.ExpectedRevision);
	// A fresh planner cannot recover when its source is missing. The retained plan can.
	TArray<FRPGInventoryProjectionEntry> Entries; F.Store.Replace(F.Owner, {}, Entries);
	F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	TestTrue(TEXT("Frozen replay submitted"), F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done));
	TestTrue(TEXT("Validated original receipt reconciles"), Outcome.WasSuccessful());
	TestEqual(TEXT("Serialized command is byte-identical, including int64 revisions"), F.Bodies[0], F.Bodies[1]);
	TestEqual(TEXT("Only one simulated durable write"), F.Writes, 1);
	TestFalse(TEXT("Resolved uncertainty released"), F.Pipeline->HasUncertainInventoryCommands());
	F.Store.Find(F.Source.GetItemId(), Before);
	TestEqual(TEXT("Exact high revision advances once"), Before.GetRevision(), R.ExpectedRevision + 1);
	TestEqual(TEXT("Original destination"), Before.GetLocation().SlotIndex, 3);
	TestEqual(TEXT("Quantity unchanged"), Before.GetQuantity(), 7);
	TestEqual(TEXT("Replay cannot enqueue a first-commit effect"), F.FirstEffects, 0);
	F.Pipeline->Move(R, Done);
	TestEqual(TEXT("Completed replay uses cache"), F.Bodies.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailClaimJournalRecoveryTest,
	"ProjectRPG.Item.InventoryRecovery.MailClaimUsesOriginalDurablePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailClaimJournalRecoveryTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F;
	FRPGItemRecord Mail;
	if (!TestTrue(TEXT("Existing record moved to mail fixture"), F.Source.TryMoveTo(
		{ERPGItemContainerType::Mail, TEXT("DungeonReward.") + Files.Scope.SessionId, 0}, Mail))) return false;
	F.Source = Mail;
	TArray<FRPGInventoryProjectionEntry> Entries;
	if (!TestTrue(TEXT("Owned mail and unrelated bag item loaded"), F.Store.Replace(F.Owner, {Mail, F.Other}, Entries))) return false;
	FRPGItemClaimMailRequest R;
	R.RequestId = FGuid::NewGuid(); R.Actor = F.Owner; R.ItemId = Mail.GetItemId();
	R.ExpectedRevision = Mail.GetRevision(); R.InventoryDestination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3};
	auto Journal = Files.Open();
	if (!TestTrue(TEXT("Mail recovery journal opened"), Journal->Restore())) return false;
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
	bool bDurableBeforeWire = false;
	F.BeforeCommit = [&]() { auto Probe = Files.Open(); bDurableBeforeWire = Probe->Restore() && Probe->GetPending().Contains(R.RequestId); };
	FRPGItemAsyncCommandOutcome O;
	auto Done = [&O](FRPGItemAsyncCommandOutcome Value) { O = MoveTemp(Value); };
	F.Pipeline->ClaimMail(R, Done);
	if (!TestTrue(TEXT("Original attachment claim is flushed before transport"), bDurableBeforeWire) ||
		!TestEqual(TEXT("Simulated commit has exactly one attachment"), F.Saved.Records.Num(), 1)) return false;
	TestEqual(TEXT("Lost claim response is not a definitive rejection"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	FName Operation;
	TestTrue(TEXT("Existing owner-only recovery can find this claim"),
		F.Pipeline->FindUncertainInventoryCommand(R.RequestId, F.Owner, Operation));
	TestEqual(TEXT("Recovery operation remains the original claim"), Operation, FName(TEXT("Item.ClaimMail")));
	FRPGItemRecord Before;
	F.Store.Find(Mail.GetItemId(), Before);
	TestEqual(TEXT("Unknown commit does not optimistically remove mail"), Before.GetLocation().ContainerType, ERPGItemContainerType::Mail);
	auto Changed = R; Changed.InventoryDestination.SlotIndex = 4;
	F.Pipeline->ClaimMail(Changed, Done);
	TestEqual(TEXT("Same ID with a different destination is refused"), O.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
	Changed.RequestId = FGuid::NewGuid();
	TestFalse(TEXT("A fresh claim ID cannot bypass the unknown result"), F.Pipeline->ClaimMail(Changed, Done));
	auto Foreign = F.Owner; Foreign.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TestFalse(TEXT("A foreign owner cannot retry this attachment"), F.Pipeline->RetryInventoryCommand(R.RequestId, Foreign, Done));
	TestEqual(TEXT("Invalid retries reached no transport"), F.Bodies.Num(), 1);
	F.BeforeCommit = {}; F.Pipeline.Reset(); Journal.Reset();
	auto Restored = Files.Open();
	if (!TestTrue(TEXT("Reconstructed journal restores the original attachment"), Restored->Restore())) return false;
	if (!TestTrue(TEXT("Latest backend snapshot already contains the claimed inventory item"),
		F.Store.Replace(F.Owner, {F.Saved.Records[0], F.Other}, Entries))) return false;
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Restored);
	TestTrue(TEXT("Restoration never automatically replays the claim"), F.Pipeline->HasUncertainInventoryCommands() && F.Bodies.Num() == 1);
	F.Reply = ERPGItemBackendStatus::Unauthorized;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("An authorization failure cannot erase an earlier unknown claim"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.bWrongRecord = true;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("A receipt with a changed destination cannot release recovery"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bWrongRecord = false; F.bWrongQuantity = true;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("A receipt with a changed affected quantity remains uncertain"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bWrongQuantity = false;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestTrue(TEXT("Original validated receipt settles the claim without replanning"), O.WasSuccessful());
	for (const FString& Body : F.Bodies) TestEqual(TEXT("Every retry preserves the exact original claim bytes"), Body, F.Bodies[0]);
	TestEqual(TEXT("Only one simulated durable item mutation"), F.Writes, 1);
	TestEqual(TEXT("An AlreadyApplied claim never runs a first-commit effect"), F.FirstEffects, 0);
	TestFalse(TEXT("Settled claim no longer blocks item commands"), F.Pipeline->HasUncertainInventoryCommands());
	auto Closed = Files.Open();
	TestTrue(TEXT("Claim resolution is also durable"), Closed->Restore() && Closed->GetPending().IsEmpty());
	FRPGItemRecord Claimed, Other;
	TestTrue(TEXT("Same attachment identity retained"), F.Store.Find(Mail.GetItemId(), Claimed));
	TestEqual(TEXT("Original exact int64 revision advances once"), Claimed.GetRevision(), R.ExpectedRevision + 1);
	TestEqual(TEXT("Original stack quantity retained"), Claimed.GetQuantity(), Mail.GetQuantity());
	TestEqual(TEXT("Original bag destination retained"), Claimed.GetLocation().SlotIndex, 3);
	TestTrue(TEXT("Unrelated item preserved"), F.Store.Find(F.Other.GetItemId(), Other) && Other.GetRevision() == F.Other.GetRevision());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMailClaimJournalValidationTest,
	"ProjectRPG.Item.InventoryRecovery.MailClaimJournalRejectsInvalidMutations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMailClaimJournalValidationTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F;
	FRPGItemRecord Mail;
	if (!F.Source.TryMoveTo({ERPGItemContainerType::Mail, TEXT("DungeonReward.journal-test"), 0}, Mail)) return false;
	TArray<FRPGInventoryProjectionEntry> Entries;
	F.Store.Replace(F.Owner, {Mail, F.Other}, Entries);
	FRPGItemClaimMailRequest R;
	R.RequestId = FGuid::NewGuid(); R.Actor = F.Owner; R.ItemId = Mail.GetItemId(); R.ExpectedRevision = Mail.GetRevision();
	R.InventoryDestination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3};
	FRPGItemActionCommandPlanner Planner(F.Store, F.Definitions, F.Actions);
	const auto Plan = Planner.Prepare(R);
	if (!TestTrue(TEXT("Valid original claim plan"), Plan.IsReady())) return false;
	for (int32 Mode = 0; Mode < 5; ++Mode)
	{
		FJournalFiles Files; auto Journal = Files.Open();
		if (!TestTrue(TEXT("Independent empty journal"), Journal->Restore())) return false;
		auto Request = Plan.CommitRequest;
		if (Mode == 0) ++Request.AffectedQuantity;
		if (Mode == 1) { const auto Extra = Request.Mutations[0]; Request.Mutations.Add(Extra); }
		if (Mode == 2) Request.Actor.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		if (Mode == 3) { Request.Mutations[0].ExpectedRevision = 0; Request.Mutations[0].NewRecord = Request.Mutations[0].NewRecord.CopyWithRevision(0); }
		if (Mode == 4) Request.Mutations[0].NewRecord = Mail; // A frozen claim must write to the character's bag, not back into mail.
		TestFalse(TEXT("Invalid attachment mutation is not persisted"), Journal->Persist(Request));
		TestTrue(TEXT("Rejected mutation cannot become a recoverable grant"), Journal->GetPending().IsEmpty());
	}
	TestFalse(TEXT("One-time consume effects are not opted into journal replay"), FRPGInventoryCommandJournal::Supports(TEXT("Item.Consume")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentJournalRecoveryTest,
	"ProjectRPG.Item.InventoryRecovery.EquipmentUsesOriginalDurablePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentJournalRecoveryTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	for (const bool bUnequip : {false, true})
	{
		FJournalFiles Files; FFixture F;
		if (!TestTrue(TEXT("Equipment and unrelated item loaded"), F.PrepareEquipment(bUnequip))) return false;
		const FName Operation = bUnequip ? TEXT("Item.Unequip") : TEXT("Item.Equip");
		const FGuid Id = FGuid::NewGuid();
		FRPGItemEquipRequest Equip;
		Equip.RequestId = Id; Equip.Actor = F.Owner; Equip.ItemId = F.Source.GetItemId();
		Equip.ExpectedRevision = F.Source.GetRevision(); Equip.EquipmentContainerId = F.Owner.OwnerId;
		Equip.SlotType = EEquipmentSlotType::Head;
		FRPGItemUnequipRequest Unequip;
		Unequip.RequestId = Id; Unequip.Actor = F.Owner; Unequip.ItemId = F.Source.GetItemId();
		Unequip.ExpectedRevision = F.Source.GetRevision();
		Unequip.InventoryDestination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3};
		auto Journal = Files.Open();
		if (!TestTrue(TEXT("Equipment journal opened"), Journal->Restore())) return false;
		F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
		bool bDurableBeforeWire = false;
		F.BeforeCommit = [&]() { auto Probe = Files.Open(); bDurableBeforeWire = Probe->Restore() && Probe->GetPending().Contains(Id); };
		FRPGItemAsyncCommandOutcome Outcome;
		auto Done = [&Outcome](FRPGItemAsyncCommandOutcome Value) { Outcome = MoveTemp(Value); };
		auto Submit = [&]() { return bUnequip ? F.Pipeline->Unequip(Unequip, Done) : F.Pipeline->Equip(Equip, Done); };
		if (!TestTrue(TEXT("Equipment submitted"), Submit()) || !TestTrue(TEXT("Original equipment plan flushed before transport"), bDurableBeforeWire)) return false;
		TestEqual(TEXT("Lost equipment response stays uncertain"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
		FRPGItemRecord Before;
		TestTrue(TEXT("No optimistic equipment transition"), F.Store.Find(F.Source.GetItemId(), Before) && Before.GetLocation() == F.Source.GetLocation());
		Equip.SlotType = EEquipmentSlotType::Chest; Unequip.InventoryDestination.SlotIndex = 4;
		Submit();
		TestEqual(TEXT("Changed same-ID equipment input is rejected"), Outcome.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
		Equip.RequestId = Unequip.RequestId = FGuid::NewGuid(); Equip.SlotType = EEquipmentSlotType::Head;
		TestFalse(TEXT("A new request cannot bypass pending recovery"), Submit());
		auto Foreign = F.Owner; Foreign.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		TestFalse(TEXT("Foreign owner cannot recover equipment"), F.Pipeline->RetryInventoryCommand(Id, Foreign, Done));
		TestEqual(TEXT("Invalid retries sent no HTTP"), F.Bodies.Num(), 1);
		F.BeforeCommit = {}; F.Pipeline.Reset(); Journal.Reset();
		auto Restored = Files.Open();
		if (!TestTrue(TEXT("Original equipment journal restored"), Restored->Restore())) return false;
		TArray<FRPGInventoryProjectionEntry> Entries;
		if (!TestTrue(TEXT("Fresh snapshot already has committed equipment state"), F.Store.Replace(F.Owner, {F.Saved.Records[0], F.Other}, Entries))) return false;
		F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Restored);
		FName RestoredOperation;
		TestTrue(TEXT("Original ID and operation exposed without automatic replay"),
			F.Pipeline->FindUncertainInventoryCommand(Id, F.Owner, RestoredOperation) && RestoredOperation == Operation && F.Bodies.Num() == 1);
		for (const auto Status : {ERPGItemBackendStatus::Unauthorized, ERPGItemBackendStatus::NotFound, ERPGItemBackendStatus::LocationConflict})
		{
			F.Reply = Status; F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
			TestEqual(TEXT("Negative retry cannot erase original equipment uncertainty"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
		}
		F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.bWrongRecord = true;
		F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
		TestEqual(TEXT("Changed receipt destination cannot unlock equipment"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
		F.bWrongRecord = false; F.bWrongQuantity = true;
		F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
		TestEqual(TEXT("Changed receipt quantity cannot unlock equipment"), Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
		F.bWrongQuantity = false; F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
		TestTrue(TEXT("Original validated equipment receipt settles recovery"), Outcome.WasSuccessful());
		for (const auto& Body : F.Bodies) TestEqual(TEXT("Every retry preserves original equipment bytes"), Body, F.Bodies[0]);
		TestEqual(TEXT("One simulated durable write"), F.Writes, 1);
		TestEqual(TEXT("AlreadyApplied never replays a one-time effect"), F.FirstEffects, 0);
		TestFalse(TEXT("Equipment recovery lock released"), F.Pipeline->HasUncertainInventoryCommands());
		auto Closed = Files.Open();
		TestTrue(TEXT("Equipment resolution is durable"), Closed->Restore() && Closed->GetPending().IsEmpty());
		FRPGItemRecord Actual, Other;
		TestTrue(TEXT("Same equipment identity remains"), F.Store.Find(F.Source.GetItemId(), Actual));
		const auto Expected = F.Saved.Records[0];
		TestTrue(TEXT("All committed equipment fields, binding and durability preserved"), FRPGItemRecord::StaticStruct()->CompareScriptStruct(&Expected, &Actual, 0));
		TestEqual(TEXT("Exact high revision advances only once"), Actual.GetRevision(), F.Source.GetRevision() + 1);
		TestTrue(TEXT("Unrelated item unchanged"), F.Store.Find(F.Other.GetItemId(), Other) && FRPGItemRecord::StaticStruct()->CompareScriptStruct(&F.Other, &Other, 0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentJournalValidationTest,
	"ProjectRPG.Item.InventoryRecovery.EquipmentJournalRejectsInvalidMutations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentJournalValidationTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	for (const bool bUnequip : {false, true})
	{
		FFixture F;
		if (!F.PrepareEquipment(bUnequip)) return false;
		FRPGItemActionCommandPlanner Planner(F.Store, F.Definitions, F.Actions);
		FRPGItemEquipRequest Equip;
		Equip.RequestId = FGuid::NewGuid(); Equip.Actor = F.Owner; Equip.ItemId = F.Source.GetItemId();
		Equip.ExpectedRevision = F.Source.GetRevision(); Equip.EquipmentContainerId = F.Owner.OwnerId; Equip.SlotType = EEquipmentSlotType::Head;
		FRPGItemUnequipRequest Unequip;
		Unequip.RequestId = FGuid::NewGuid(); Unequip.Actor = F.Owner; Unequip.ItemId = F.Source.GetItemId();
		Unequip.ExpectedRevision = F.Source.GetRevision(); Unequip.InventoryDestination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3};
		const auto Plan = bUnequip ? Planner.Prepare(Unequip) : Planner.Prepare(Equip);
		if (!TestTrue(TEXT("Valid server equipment plan"), Plan.IsReady())) return false;
		for (int32 Mode = 0; Mode < 11; ++Mode)
		{
			FJournalFiles Files; auto Journal = Files.Open();
			if (!Journal->Restore()) return false;
			auto Request = Plan.CommitRequest;
			auto& Mutation = Request.Mutations[0];
			if (Mode == 0) Request.AffectedQuantity = 2;
			if (Mode == 1) { const auto Extra = Mutation; Request.Mutations.Add(Extra); }
			if (Mode == 2) Request.Actor.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
			if (Mode == 3) { Mutation.ExpectedRevision = 0; Mutation.NewRecord = Mutation.NewRecord.CopyWithRevision(0); }
			if (Mode >= 4)
			{
				const auto Original = Mutation.NewRecord;
				auto Location = Original.GetLocation(); auto Metadata = Original.GetMetadata();
				if (Mode == 4) Location.ContainerType = bUnequip ? ERPGItemContainerType::Equipment : ERPGItemContainerType::Inventory;
				if (Mode == 5) Location.ContainerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
				if (Mode == 6) Location.SlotIndex = bUnequip ? RPGItemInventoryPolicy::SlotCapacity : static_cast<int32>(EEquipmentSlotType::Count);
				if (Mode == 7) Location.ContainerType = ERPGItemContainerType::Mail;
				if (Mode == 8) Metadata.bLocked = true;
				if (!TestTrue(TEXT("Malformed but structurally valid equipment fixture"), FRPGItemRecord::TryRestore(
					Original.GetDefinitionId(), Original.GetDefinitionVersion(), Original.GetOwner(), Location, Original.GetState(),
					Original.GetRevision(), Original.GetLifecycleState(), Metadata, Mutation.NewRecord))) return false;
				if (Mode == 9) Mutation.NewRecord.TrySetQuantity(2, Mutation.NewRecord);
				if (Mode == 10) Mutation.NewRecord.TryMarkConsumed(Mutation.NewRecord);
			}
			TestFalse(TEXT("Invalid equipment plan cannot become replay authority"), Journal->Persist(Request));
			TestTrue(TEXT("Invalid equipment plan leaves no pending command"), Journal->GetPending().IsEmpty());
		}
	}
	TestFalse(TEXT("Consume remains excluded until its one-time effect protocol exists"), FRPGInventoryCommandJournal::Supports(TEXT("Item.Consume")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryCommandRecoveryGuardTest,
	"ProjectRPG.Item.InventoryRecovery.OwnerAndFrozenIntentGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryCommandRecoveryGuardTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F; auto R = F.Move(); FRPGItemAsyncCommandOutcome O;
	auto Done = [&O](FRPGItemAsyncCommandOutcome V) { O = MoveTemp(V); };
	F.Pipeline->Move(R, Done);
	auto Wrong = F.Owner; Wrong.OwnerId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	TestFalse(TEXT("Foreign owner cannot replay"), F.Pipeline->RetryInventoryCommand(R.RequestId, Wrong, Done));
	TestFalse(TEXT("Unknown request cannot replay"), F.Pipeline->RetryInventoryCommand(FGuid::NewGuid(), F.Owner, Done));
	auto Changed = R; Changed.Destination.SlotIndex = 4; F.Pipeline->Move(Changed, Done);
	TestEqual(TEXT("Changed payload conflicts with original ID"), O.PlanFailureStatus, ERPGItemTransactionStatus::IdempotencyConflict);
	Changed.RequestId = FGuid::NewGuid();
	TestFalse(TEXT("New ID cannot bypass an unresolved save"), F.Pipeline->Move(Changed, Done));
	TestEqual(TEXT("Guards did not reach transport"), F.Bodies.Num(), 1);
	F.Pipeline->Move(R, Done);
	TestEqual(TEXT("Normal RPC replay cannot start a second recovery path"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bHold = true; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestFalse(TEXT("Concurrent retry coalesced"), F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done));
	TestEqual(TEXT("Exactly one retry outstanding"), F.Bodies.Num(), 2);
	auto Finish = MoveTemp(F.Held); auto Receipt = F.Saved; Receipt.Status = ERPGItemBackendStatus::AlreadyApplied; Finish(Receipt);
	TestTrue(TEXT("Outstanding retry can finish"), O.WasSuccessful());
	FFixture Ended; Ended.bHold = true; bool bLateApplied = false;
	Ended.Pipeline->Move(Ended.Move(), [&bLateApplied](FRPGItemAsyncCommandOutcome) { bLateApplied = true; });
	Ended.Pipeline.Reset(); auto Late = MoveTemp(Ended.Held);
	auto LateReceipt = Ended.Saved; LateReceipt.Status = ERPGItemBackendStatus::AlreadyApplied; Late(LateReceipt);
	TestFalse(TEXT("Teardown invalidates deferred callbacks"), bLateApplied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryCommandRecoveryFailureTest,
	"ProjectRPG.Item.InventoryRecovery.FailedAndMismatchedRecoveryStaysLocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryCommandRecoveryFailureTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F; auto R = F.Move(); FRPGItemAsyncCommandOutcome O;
	auto Done = [&O](FRPGItemAsyncCommandOutcome V) { O = MoveTemp(V); };
	F.Pipeline->Move(R, Done);
	for (auto Status : {ERPGItemBackendStatus::Unauthorized, ERPGItemBackendStatus::Forbidden,
		ERPGItemBackendStatus::NotFound, ERPGItemBackendStatus::RevisionConflict, ERPGItemBackendStatus::ServerError})
	{
		F.Reply = Status; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
		TestEqual(TEXT("Negative retry is not evidence the earlier commit never happened"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
		TestEqual(TEXT("Original payload preserved on every attempt"), F.Bodies.Last(), F.Bodies[0]);
	}
	F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.bWrongReceipt = true;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("Mismatched success cannot unlock"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bWrongReceipt = false; F.bWrongRecord = true; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("Same receipt ID with changed record cannot unlock"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bWrongRecord = false; F.bWrongQuantity = true; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestEqual(TEXT("Changed receipt quantity cannot unlock"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	F.bWrongQuantity = false; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestTrue(TEXT("Matching success resolves"), O.WasSuccessful());
	FFixture Rejected; Rejected.Reply = ERPGItemBackendStatus::LocationConflict;
	Rejected.Pipeline->Move(Rejected.Move(), Done);
	TestEqual(TEXT("Initial definite conflict is still a normal rejection"), O.Stage, ERPGItemAsyncCommandStage::BackendRejected);
	TestFalse(TEXT("Initial definite conflict does not create recovery"), Rejected.Pipeline->HasUncertainInventoryCommands());
	FFixture LostThenUnauthorized; LostThenUnauthorized.Reply = ERPGItemBackendStatus::Unauthorized;
	LostThenUnauthorized.bEarlierAttemptUncertain = true;
	LostThenUnauthorized.Pipeline->Move(LostThenUnauthorized.Move(), Done);
	TestEqual(TEXT("Gateway attempt history survives the final auth error"), O.Stage, ERPGItemAsyncCommandStage::Uncertain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryCommandRecoveryPairTest,
	"ProjectRPG.Item.InventoryRecovery.SplitMergeAndSwapFrozenMutations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryCommandRecoveryPairTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FFixture F; const FGuid Id = FGuid::NewGuid(); FRPGItemAsyncCommandOutcome O;
		auto Done = [&O](FRPGItemAsyncCommandOutcome V) { O = MoveTemp(V); };
		if (Mode == 0)
		{
			FRPGItemSplitRequest R; R.RequestId = Id; R.Actor = F.Owner; R.ItemId = F.Source.GetItemId();
			R.ExpectedRevision = F.Source.GetRevision(); R.Quantity = 2;
			R.Destination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3}; F.Pipeline->Split(R, Done);
		}
		else if (Mode == 1)
		{
			FRPGItemStackTransferRequest R; R.RequestId = Id; R.Actor = F.Owner; R.SourceItemId = F.Source.GetItemId();
			R.ExpectedSourceRevision = F.Source.GetRevision(); R.DestinationItemId = F.Other.GetItemId();
			R.ExpectedDestinationRevision = F.Other.GetRevision(); R.RequestedQuantity = MAX_int32; F.Pipeline->TransferStack(R, Done);
		}
		else
		{
			FRPGItemSwapRequest R; R.RequestId = Id; R.Actor = F.Owner; R.SourceItemId = F.Source.GetItemId();
			R.ExpectedSourceRevision = F.Source.GetRevision(); R.DestinationItemId = F.Other.GetItemId();
			R.ExpectedDestinationRevision = F.Other.GetRevision(); R.InventoryContainerId = F.Owner.OwnerId; F.Pipeline->Swap(R, Done);
		}
		if (!TestEqual(TEXT("Pair operation becomes uncertain"), O.Stage, ERPGItemAsyncCommandStage::Uncertain)) continue;
		TestEqual(TEXT("Both mutations retained"), F.Saved.Records.Num(), 2);
		F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
		TestTrue(TEXT("Pair receipt applied atomically"), O.WasSuccessful());
		TestEqual(TEXT("Both mutations and generated split ID remain byte-identical"), F.Bodies[0], F.Bodies[1]);
		TestEqual(TEXT("No duplicate simulated write"), F.Writes, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryCommandRecoveryUITest,
	"ProjectRPG.Item.InventoryRecovery.ReservationsAndReopenedViewModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryCommandRecoveryUITest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F;
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Recovery fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* PC = World->SpawnActor<APlayerController>();
	if (!PC) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
	auto* Projection = NewObject<URPGInventoryProjectionComponent>(PC); PC->AddInstanceComponent(Projection); Projection->RegisterComponent();
	auto* Commands = NewObject<URPGItemCommandComponent>(PC); PC->AddInstanceComponent(Commands); Commands->RegisterComponent();
	TestTrue(TEXT("Recovery projection initialized"), Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, F.Other}));
	TestTrue(TEXT("Recovery projection Ready"), Projection->ApplyCompletedRewardSnapshot(F.Owner, {F.Source, F.Other}, Projection->GetAuthoritativeSnapshotGeneration()));
	auto* Model = NewObject<URPGInventoryProjectionViewModel>(PC); Model->InitializeFromPlayerController(PC);
	const FGuid Id = FGuid::NewGuid(); auto& Pending = Model->PendingInventoryMovesByRequest.Add(Id);
	Pending.ItemId = F.Source.GetItemId(); Pending.ExpectedRevision = F.Source.GetRevision(); Pending.DestinationSlot = 3;
	FRPGItemCommandClientResult Result; Result.RequestId = Id; Result.Operation = TEXT("Item.Move"); Result.Result = ERPGItemCommandResultCode::Uncertain;
	Model->HandleCommandCompleted(Result);
	TestTrue(TEXT("Unknown outcome does not release reservation"), Model->IsInventorySlotReserved(3));
	auto& State = Commands->InventoryRecovery.AddDefaulted_GetRef(); State.RequestId = Id; State.Operation = TEXT("Item.Move");
	Commands->OnInventoryRecoveryChanged.Broadcast();
	TestTrue(TEXT("Recovery button state available to BP"), Model->bHasInventoryRecovery && Model->bCanRetryInventoryRecovery);
	FRPGInventoryProjectionEntry Drag; TestFalse(TEXT("New drag blocked"), Model->TryGetInventoryDragEntry(F.Source.GetItemId(), Drag));
	Model->InitializeFromPlayerController(nullptr); Model->InitializeFromPlayerController(PC);
	TestTrue(TEXT("Reopening cannot forget server recovery"), Model->bHasInventoryRecovery);
	// A failed fast retry can leave the replicated entry identical. Its ACK must still finish local UI progress.
	Model->LocalInventoryRecoveryRequest = Id;
	Model->NextInventoryRecoveryRequestTime = FPlatformTime::Seconds() + 3.0;
	Model->HandleInventoryRecoveryChanged();
	TestTrue(TEXT("Local click immediately disables the retry"), Model->bInventoryRecoveryInFlight && !Model->bCanRetryInventoryRecovery);
	Model->HandleCommandCompleted(Result);
	TestFalse(TEXT("Identical Uncertain ACK completes local progress without a new OnRep"), Model->bInventoryRecoveryInFlight);
	TestFalse(TEXT("Failure still respects cooldown"), Model->bCanRetryInventoryRecovery);
	Model->NextInventoryRecoveryRequestTime = 0.0;
	TestFalse(TEXT("Availability timer ends after cooldown"), Model->TickInventoryRecoveryCooldown(0.1f));
	TestTrue(TEXT("Timer permits retry without unlocking the original uncertain command"), Model->bHasInventoryRecovery && Model->bCanRetryInventoryRecovery);
	Commands->InventoryRecovery[0].bRetrying = true; Commands->OnInventoryRecoveryChanged.Broadcast();
	TestFalse(TEXT("Outstanding recovery disables repeat button"), Model->bCanRetryInventoryRecovery);
	Commands->InventoryRecovery.Reset(); Commands->OnInventoryRecoveryChanged.Broadcast();
	TestFalse(TEXT("Confirmed recovery removes global lock"), Model->bHasInventoryRecovery);
	for (const bool bUnequip : {false, true})
	{
		FRPGItemRecord Source = F.Other;
		const FRPGItemLocation EquipmentLocation{ERPGItemContainerType::Equipment, F.Owner.OwnerId, static_cast<int32>(EEquipmentSlotType::Head)};
		if (bUnequip && !Source.TryEquipTo(EquipmentLocation, Source)) return false;
		if (!TestTrue(TEXT("Original equipment UI snapshot"), Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, Source}))) return false;
		const FGuid ActionId = FGuid::NewGuid();
		auto& Action = Model->PendingItemActionsByRequest.Add(ActionId);
		Action.ItemId = Source.GetItemId(); Action.ExpectedRevision = Source.GetRevision();
		Action.SourceContainer = Source.GetLocation().ContainerType; Action.SourceSlot = Source.GetLocation().SlotIndex;
		Action.DestinationContainer = bUnequip ? ERPGItemContainerType::Inventory : ERPGItemContainerType::Equipment;
		Action.DestinationSlot = bUnequip ? 3 : EquipmentLocation.SlotIndex;
		Model->RefreshPendingItemCommandCount();
		auto& Recovery = Commands->InventoryRecovery.AddDefaulted_GetRef();
		Recovery.RequestId = ActionId; Recovery.Operation = bUnequip ? TEXT("Item.Unequip") : TEXT("Item.Equip");
		Commands->OnInventoryRecoveryChanged.Broadcast();
		Result.RequestId = ActionId; Result.Operation = Recovery.Operation; Result.Result = ERPGItemCommandResultCode::Conflict;
		Model->HandleCommandCompleted(Result);
		TestTrue(TEXT("Rejected retry retains original equipment action"), Model->IsItemActionPending(Source.GetItemId()));
		TestTrue(TEXT("Rejected retry retains equipment slot reservation"), Model->IsEquipmentSlotReserved(EquipmentLocation.SlotIndex));
		TestTrue(TEXT("Rejected retry retains bag slot reservation"), Model->IsInventorySlotReserved(bUnequip ? 3 : Source.GetLocation().SlotIndex));
		TestTrue(TEXT("Existing BP recovery remains available"), Model->bHasInventoryRecovery && Model->bCanRetryInventoryRecovery);
		Commands->InventoryRecovery.Reset(); Commands->OnInventoryRecoveryChanged.Broadcast();
		Result.Result = ERPGItemCommandResultCode::AlreadyApplied; Model->HandleCommandCompleted(Result);
		TestTrue(TEXT("Recovery ACK alone cannot release old equipment projection"), Model->IsItemActionPending(Source.GetItemId()));
		FRPGItemRecord Committed;
		const bool bTransition = bUnequip ? Source.TryMoveTo({ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3}, Committed) : Source.TryEquipTo(EquipmentLocation, Committed);
		if (!TestTrue(TEXT("Committed equipment transition constructed"), bTransition)) return false;
		Committed = Committed.CopyWithRevision(Source.GetRevision() + 1);
		if (!TestTrue(TEXT("Confirmed equipment projection applied"), Projection->ApplyAuthoritativeRecords(F.Owner, {F.Source, Committed}))) return false;
		TestFalse(TEXT("New authoritative revision retires equipment reservation"), Model->IsItemActionPending(Source.GetItemId()));
		TestFalse(TEXT("Equipment destination no longer locally reserved"), Model->IsEquipmentSlotReserved(EquipmentLocation.SlotIndex));
	}
	Model->InitializeFromPlayerController(nullptr); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryRecoveryRevokedUITest,
	"ProjectRPG.Item.InventoryRecovery.RevokedIdentityRetiresPendingUIAndLateReply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryRecoveryRevokedUITest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F; FUIFixture UI(F);
	if (!TestNotNull(TEXT("Authenticated UI fixture"), UI.Model)) return false;
	F.LiveCommands = UI.Commands;
	FRPGInventoryRecoveryRevocationTestAccess::Attach(*UI.Commands, F.Pipeline);
	const auto R = F.Move();
	const auto Done = [&](FRPGItemAsyncCommandOutcome Outcome)
	{ FRPGInventoryRecoveryRevocationTestAccess::Finish(*UI.Commands, R.RequestId, MoveTemp(Outcome)); };
	F.Pipeline->Move(R, Done);
	TestTrue(TEXT("Real facade publishes uncertain command"), UI.Model->bHasInventoryRecovery);
	auto& Pending = UI.Model->PendingInventoryMovesByRequest.Add(R.RequestId);
	Pending.ItemId = R.ItemId; Pending.ExpectedRevision = R.ExpectedRevision; Pending.DestinationSlot = 3;
	UI.Model->LocalInventoryRecoveryRequest = R.RequestId;
	UI.Model->NextInventoryRecoveryRequestTime = FPlatformTime::Seconds() + 3.0;
	UI.Model->HandleInventoryRecoveryChanged();
	F.bHold = true; F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	if (!TestTrue(TEXT("Original recovery response held"), F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done)) ||
		!TestTrue(TEXT("Deferred callback exists"), static_cast<bool>(F.Held))) return false;
	TWeakPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> WeakPipeline = F.Pipeline;
	F.Pipeline.Reset(); // The real component now owns the only strong reference.
	bool bObserverSawBarrier = false;
	const auto Notification = UI.Model->AddFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bInventoryAuthorityRevoked,
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateLambda([&](UObject*, UE::FieldNotification::FFieldId)
		{
			bObserverSawBarrier = UI.Model->bInventoryAuthorityRevoked && !UI.Commands->HasInFlightCommands() && !WeakPipeline.IsValid();
		}));
	auto* RemainingState = UI.World->SpawnActor<ARPGPlayerState>();
	RemainingState->SetAuthenticatedIdentity(TEXT("11111111-2222-3333-4444-555555555557"), TEXT("76561198000000002"), UI.Session);
	TestTrue(TEXT("Production identity revocation"), UI.State->RevokeAuthenticatedIdentity(UI.Session, F.Owner.OwnerId, UI.Steam));
	TestTrue(TEXT("Re-entrant UI observers see all command barriers already raised"), bObserverSawBarrier);
	UI.Model->RemoveFieldValueChangedDelegate(
		URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor::bInventoryAuthorityRevoked, Notification);
	TestTrue(TEXT("Owner terminal state exposed to BP"), UI.Commands->IsItemCommandAuthorityRevoked() && UI.Model->bInventoryAuthorityRevoked);
	TestFalse(TEXT("No retry, progress or misleading successful recovery"), UI.Model->bHasInventoryRecovery || UI.Model->bCanRetryInventoryRecovery ||
		UI.Model->bInventoryRecoveryInFlight || UI.Model->bHasLastCommandResult);
	TestTrue(TEXT("Local reservations retired without rolling back persistence"), UI.Model->PendingInventoryMovesByRequest.IsEmpty());
	TestTrue(TEXT("Revoked view is empty"), UI.Model->InventoryItems.IsEmpty() && UI.Model->EquipmentItems.IsEmpty() && UI.Model->MailItems.IsEmpty());
	TestFalse(TEXT("Real component discarded deferred pipeline"), WeakPipeline.IsValid());
	TestFalse(TEXT("No owner command remains in flight"), UI.Commands->HasInFlightCommands());
	const int32 WireCalls = F.Bodies.Num();
	UI.Commands->ServerRetryUncertainInventoryCommand(R.RequestId);
	TestEqual(TEXT("Old identity cannot send another retry"), F.Bodies.Num(), WireCalls);
	auto Late = MoveTemp(F.Held); auto Receipt = F.Saved; Receipt.Status = ERPGItemBackendStatus::AlreadyApplied;
	Late(MoveTemp(Receipt));
	TestEqual(TEXT("Late successful response never enters real commit sink"), F.ApplyCalls, 0);
	TestEqual(TEXT("Exactly one simulated durable write remains"), F.Writes, 1);
	TestEqual(TEXT("Stored result is not undone"), F.Saved.Records[0].GetLocation().SlotIndex, 3);
	TestEqual(TEXT("Original stored revision unchanged"), F.Saved.Records[0].GetRevision(), R.ExpectedRevision + 1);
	TestTrue(TEXT("Remaining party identity is preserved"), RemainingState->HasAuthenticatedCharacter());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryRecoveryRevokedClientOrderingTest,
	"ProjectRPG.Item.InventoryRecovery.RevocationDominatesReorderedClientNotifications",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryRecoveryRevokedClientOrderingTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FFixture F; FUIFixture UI(F);
	if (!TestNotNull(TEXT("UI fixture"), UI.Model)) return false;
	TestEqual(TEXT("Ready snapshot positive control"), UI.Model->InventoryItems.Num(), 2);
	// Simulate owner replication ordering, not a backend abandonment receipt or Iris E2E.
	FRPGInventoryRecoveryRevocationTestAccess::ReplicateRevocation(*UI.Commands);
	auto& Stale = UI.Commands->InventoryRecovery.AddDefaulted_GetRef(); Stale.RequestId = FGuid::NewGuid(); Stale.Operation = TEXT("Item.Move");
	FRPGInventoryRecoveryRevocationTestAccess::ReplicateRecovery(*UI.Commands);
	UI.Projection->OnLoadStateChanged.Broadcast(ERPGInventoryProjectionLoadState::Ready);
	UI.Projection->OnProjectionChanged.Broadcast();
	FRPGItemCommandClientResult Late; Late.RequestId = Stale.RequestId; Late.Operation = Stale.Operation; Late.Result = ERPGItemCommandResultCode::AlreadyApplied;
	UI.Commands->OnItemCommandCompleted.Broadcast(Late);
	FRPGDungeonRewardDeliveryNotice Reward; Reward.bApplied = true; Reward.InventoryQuantity = 3;
	UI.State->OnDungeonRewardDelivered.Broadcast(Reward);
	TestTrue(TEXT("Terminal state dominates stale recovery array"), UI.Model->bInventoryAuthorityRevoked && !UI.Model->bCanRetryInventoryRecovery && !UI.Model->bHasInventoryRecovery);
	TestTrue(TEXT("Late snapshots stay hidden"), UI.Model->InventoryItems.IsEmpty() && UI.Model->LoadState == ERPGInventoryProjectionLoadState::Uninitialized);
	TestFalse(TEXT("Late success ACK is not displayed"), UI.Model->bHasLastCommandResult);
	TestTrue(TEXT("Late reward message is not displayed"), UI.Model->RewardDeliveryMessage.IsEmpty());
	FGuid NewRequest;
	TestFalse(TEXT("Stale item cannot be moved"), UI.Model->RequestMoveItem(F.Source.GetItemId(), F.Source.GetRevision(), 3, NewRequest));
	TestFalse(TEXT("Stale item cannot be consumed"), UI.Model->RequestConsume(F.Source.GetItemId(), NewRequest));
	TestFalse(TEXT("Recovery cannot be sent"), UI.Model->RequestRetryInventoryCommand());
	TestFalse(TEXT("Snapshot reload cannot be sent"), UI.Model->RequestRetryLoad());
	UI.Model->InitializeFromPlayerController(UI.PC);
	TestTrue(TEXT("Reopening reads component terminal state, not old ready snapshot"), UI.Model->bInventoryAuthorityRevoked && UI.Model->InventoryItems.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalRestartTest,
	"ProjectRPG.Item.InventoryRecovery.JournalWriteAheadAndReconstructedPipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalRestartTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; auto Journal = Files.Open();
	if (!TestTrue(TEXT("Clean server journal"), Journal->Restore())) return false;
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
	bool bDurableBeforeWire = false;
	F.BeforeCommit = [&]() { auto Probe = Files.Open(); bDurableBeforeWire = Probe->Restore() && Probe->GetPending().Num() == 1; };
	F.bHold = true; const auto R = F.Move(); FRPGItemAsyncCommandOutcome Outcome;
	F.Pipeline->Move(R, [](FRPGItemAsyncCommandOutcome) {});
	TestTrue(TEXT("A separate reader sees the flushed original plan before transport starts"), bDurableBeforeWire);
	TestEqual(TEXT("Pending stored before any completion"), Journal->GetPending().Num(), 1);
	F.BeforeCommit = {}; F.Pipeline.Reset(); Journal.Reset();
	auto Restored = Files.Open();
	if (!TestTrue(TEXT("New journal instance restores real disk bytes"), Restored->Restore())) return false;
	TArray<FRPGInventoryProjectionEntry> Entries; F.Store.Replace(F.Owner, {}, Entries);
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Restored);
	TestTrue(TEXT("Fresh pipeline starts uncertain, not automatically sent"), F.Pipeline->HasUncertainInventoryCommands() && F.Bodies.Num() == 1);
	F.bHold = false; F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, [&](FRPGItemAsyncCommandOutcome O) { Outcome = MoveTemp(O); });
	TestTrue(TEXT("Validated original receipt reconciles without replanning missing source"), Outcome.WasSuccessful());
	TestEqual(TEXT("All original bytes, large int64 revisions and metadata retained"), F.Bodies[0], F.Bodies[1]);
	TestEqual(TEXT("Exactly one simulated backend write"), F.Writes, 1);
	auto Closed = Files.Open();
	TestTrue(TEXT("Resolution is also durable"), Closed->Restore() && Closed->GetPending().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalScopeTest,
	"ProjectRPG.Item.InventoryRecovery.JournalCorruptionAndIdentityScopes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalScopeTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; F.Pipeline->Move(F.Move(), [](FRPGItemAsyncCommandOutcome) {});
	FRPGItemRepositoryCommitRequest R; if (!TestTrue(TEXT("Frozen request decoded"), FRPGItemBackendJsonCodec::DeserializeCommitRequest(F.Bodies[0], R))) return false;
	auto Journal = Files.Open();
	if (!TestTrue(TEXT("Original plan persisted"), Journal->Restore() && Journal->Persist(R))) return false;
	FString Contents; FFileHelper::LoadFileToString(Contents, *Journal->GetPath());
	TestFalse(TEXT("No bearer/service credential is stored"), Contents.Contains(TEXT("Bearer ")) || Contents.Contains(TEXT("accessToken")));
	for (int32 Mode = 0; Mode < 5; ++Mode)
	{
		auto Other = Files.Scope;
		if (Mode == 0) Other.BackendUrl = TEXT("http://127.0.0.1:3001/api");
		if (Mode == 1) Other.ServerId += TEXT("-other");
		if (Mode == 2) Other.SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		if (Mode == 3) Other.CharacterId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		if (Mode == 4) Other.SteamId = TEXT("76561198000000002");
		FRPGInventoryCommandJournal Foreign(Files.Directory, Other);
		FFileHelper::SaveStringToFile(Contents, *Foreign.GetPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		TestFalse(TEXT("Copied file cannot grant a foreign backend/allocation/session/character/account recovery"), Foreign.Restore());
	}
	FFileHelper::SaveStringToFile(TEXT("{partial"), *(Journal->GetPath() + TEXT(".tmp")));
	auto Broken = Files.Open();
	TestFalse(TEXT("A partial temporary file is not an empty journal"), Broken->Restore());
	TestTrue(TEXT("Original evidence survives corrupt temporary file"), IFileManager::Get().FileExists(*Journal->GetPath()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalTemporaryTest,
	"ProjectRPG.Item.InventoryRecovery.JournalInterruptedSnapshotPromotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalTemporaryTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; F.Pipeline->Move(F.Move(), [](FRPGItemAsyncCommandOutcome) {});
	FRPGItemRepositoryCommitRequest R; FRPGItemBackendJsonCodec::DeserializeCommitRequest(F.Bodies[0], R);
	auto Journal = Files.Open();
	if (!TestTrue(TEXT("Advance snapshot generation"), Journal->Restore() && Journal->Persist(R) && Journal->ForgetVerified(R))) return false;
	R.RequestId = FGuid::NewGuid();
	if (!TestTrue(TEXT("Third generation frozen command"), Journal->Persist(R))) return false;
	FString Body; FFileHelper::LoadFileToString(Body, *Journal->GetPath());
	if (!TestTrue(TEXT("Simulate flushed temporary without final"), FFileHelper::SaveStringToFile(Body, *(Journal->GetPath() + TEXT(".tmp"))) &&
		IFileManager::Get().Delete(*Journal->GetPath(), true))) return false;
	auto Restored = Files.Open();
	TestTrue(TEXT("Complete interrupted generation restored, not silently lost"), Restored->Restore() && Restored->GetPending().Contains(R.RequestId));
	TestFalse(TEXT("Promoted temporary is gone"), IFileManager::Get().FileExists(*(Journal->GetPath() + TEXT(".tmp"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalQuarantineTest,
	"ProjectRPG.Item.InventoryRecovery.JournalRevocationCannotBecomeReplayAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalQuarantineTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; F.Pipeline->Move(F.Move(), [](FRPGItemAsyncCommandOutcome) {});
	FRPGItemRepositoryCommitRequest R; FRPGItemBackendJsonCodec::DeserializeCommitRequest(F.Bodies[0], R);
	auto Journal = Files.Open();
	if (!TestTrue(TEXT("Scoped revocation durable"), Journal->Restore() && Journal->Persist(R) && Journal->Quarantine())) return false;
	auto Restored = Files.Open();
	if (!TestTrue(TEXT("Revocation and original evidence survive reconstruction"), Restored->Restore() && Restored->IsRevoked() && Restored->GetPending().Num() == 1)) return false;
	TestFalse(TEXT("Quarantine cannot be removed as if resolved"), Restored->ForgetVerified(R));
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Restored);
	const int32 Calls = F.Bodies.Num();
	TestTrue(TEXT("Revoked file is a barrier, not imported replay authority"), F.Pipeline->IsInventoryPersistenceBlocked());
	F.Pipeline->Move(F.Move(), [](FRPGItemAsyncCommandOutcome) {});
	TestEqual(TEXT("No new backend call from revoked storage"), F.Bodies.Num(), Calls);
	FString Contents; FFileHelper::LoadFileToString(Contents, *Journal->GetPath());
	TSharedPtr<FJsonObject> Json; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Json);
	Json->SetBoolField(TEXT("revoked"), false); Json->SetStringField(TEXT("generation"), TEXT("3"));
	FString Unrevoked; FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Unrevoked));
	FFileHelper::SaveStringToFile(Unrevoked, *(Journal->GetPath() + TEXT(".tmp")));
	auto Conflict = Files.Open();
	TestFalse(TEXT("Temporary snapshot cannot undo an existing revocation"), Conflict->Restore());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalFailureTest,
	"ProjectRPG.Item.InventoryRecovery.JournalIoFailurePreventsDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalFailureTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; auto Journal = Files.Open();
	if (!TestTrue(TEXT("Initialize storage"), Journal->Restore())) return false;
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
	if (!TestTrue(TEXT("Simulate unusable target path in fixture"), IFileManager::Get().MakeDirectory(*Journal->GetPath()))) return false;
	FRPGItemAsyncCommandOutcome O;
	F.Pipeline->Move(F.Move(), [&](FRPGItemAsyncCommandOutcome Value) { O = MoveTemp(Value); });
	TestEqual(TEXT("Disk failure is distinguished from a network rejection"), O.Stage, ERPGItemAsyncCommandStage::PersistenceRejected);
	TestEqual(TEXT("No HTTP/simulated durable write before write-ahead succeeds"), F.Bodies.Num(), 0);
	TestTrue(TEXT("Future writes stay blocked"), F.Pipeline->IsInventoryPersistenceBlocked());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalUITest,
	"ProjectRPG.Item.InventoryRecovery.JournalStorageUiAndSameCharacterSessionChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalUITest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; FUIFixture UI(F);
	if (!TestNotNull(TEXT("Authenticated UI"), UI.Model)) return false;
	auto Journal = Files.Open();
	if (!TestTrue(TEXT("Current identity storage"), Journal->Restore())) return false;
	FRPGInventoryRecoveryRevocationTestAccess::AttachJournal(*UI.Commands, Journal);
	TestTrue(TEXT("Original identity positive control"), UI.Commands->IsAuthenticatedItemOwner(F.Owner));
	UI.State->SetAuthenticatedIdentity(F.Owner.OwnerId, UI.Steam, TEXT("11111111-2222-3333-4444-555555555558"));
	TestFalse(TEXT("Same character in another session cannot use old pipeline/sinks"), UI.Commands->IsAuthenticatedItemOwner(F.Owner));
	FRPGInventoryRecoveryRevocationTestAccess::BlockStorage(*UI.Commands);
	TestTrue(TEXT("Storage barrier exposed to BP without a fabricated receipt"), UI.Model->bInventoryRecoveryStorageBlocked && UI.Model->bHasInventoryRecovery);
	TestFalse(TEXT("Storage failure cannot retry or unlock"), UI.Model->bCanRetryInventoryRecovery);
	FGuid Id;
	TestFalse(TEXT("Consume blocked from old visible snapshot"), UI.Model->RequestConsume(F.Source.GetItemId(), Id));
	TestFalse(TEXT("Move blocked"), UI.Model->RequestMoveItem(F.Source.GetItemId(), F.Source.GetRevision(), 3, Id));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalPairsTest,
	"ProjectRPG.Item.InventoryRecovery.JournalSplitMergeSwapRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalPairsTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	for (int32 Mode = 0; Mode < 3; ++Mode)
	{
		FJournalFiles Files; FFixture F; auto Journal = Files.Open();
		if (!TestTrue(TEXT("Pair journal ready"), Journal->Restore())) return false;
		F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
		const FGuid Id = FGuid::NewGuid(); FRPGItemAsyncCommandOutcome O;
		auto Done = [&](FRPGItemAsyncCommandOutcome V) { O = MoveTemp(V); };
		if (Mode == 0)
		{
			FRPGItemSplitRequest R; R.RequestId = Id; R.Actor = F.Owner; R.ItemId = F.Source.GetItemId();
			R.ExpectedRevision = F.Source.GetRevision(); R.Quantity = 2;
			R.Destination = {ERPGItemContainerType::Inventory, F.Owner.OwnerId, 3}; F.Pipeline->Split(R, Done);
		}
		else if (Mode == 1)
		{
			FRPGItemStackTransferRequest R; R.RequestId = Id; R.Actor = F.Owner; R.SourceItemId = F.Source.GetItemId();
			R.ExpectedSourceRevision = F.Source.GetRevision(); R.DestinationItemId = F.Other.GetItemId();
			R.ExpectedDestinationRevision = F.Other.GetRevision(); R.RequestedQuantity = MAX_int32; F.Pipeline->TransferStack(R, Done);
		}
		else
		{
			FRPGItemSwapRequest R; R.RequestId = Id; R.Actor = F.Owner; R.SourceItemId = F.Source.GetItemId();
			R.ExpectedSourceRevision = F.Source.GetRevision(); R.DestinationItemId = F.Other.GetItemId();
			R.ExpectedDestinationRevision = F.Other.GetRevision(); R.InventoryContainerId = F.Owner.OwnerId; F.Pipeline->Swap(R, Done);
		}
		if (!TestEqual(FString::Printf(TEXT("Pair mode %d persisted despite lost result"), Mode),
			static_cast<int32>(O.Stage), static_cast<int32>(ERPGItemAsyncCommandStage::Uncertain))) continue;
		auto Reopened = Files.Open();
		if (!TestTrue(TEXT("Both mutations restore"), Reopened->Restore() && Reopened->GetPending().Contains(Id))) continue;
		if (Mode == 2) TestEqual(TEXT("Swap retains the existing zero affected-quantity wire contract"), Reopened->GetPending().FindChecked(Id).AffectedQuantity, 0);
		F.Pipeline.Reset(); F.Reply = ERPGItemBackendStatus::AlreadyApplied;
		F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Reopened);
		F.Pipeline->RetryInventoryCommand(Id, F.Owner, Done);
		TestTrue(TEXT("Frozen pair reconciles after reconstruction"), O.WasSuccessful());
		TestEqual(TEXT("Generated split ID, terminal merge source and both revisions stay byte-identical"), F.Bodies[0], F.Bodies[1]);
		TestEqual(TEXT("One simulated write"), F.Writes, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryJournalResolutionTest,
	"ProjectRPG.Item.InventoryRecovery.JournalNegativeRetryAndCleanupFailureRetainOriginal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryJournalResolutionTest::RunTest(const FString&)
{
	using namespace RPGInventoryCommandRecoveryTests;
	FJournalFiles Files; FFixture F; auto Journal = Files.Open();
	if (!TestTrue(TEXT("Resolution journal ready"), Journal->Restore())) return false;
	F.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(F.Store, F.Definitions, F.Actions, F, F, F, Journal);
	const auto R = F.Move(); FRPGItemAsyncCommandOutcome O;
	auto Done = [&](FRPGItemAsyncCommandOutcome V) { O = MoveTemp(V); };
	F.Pipeline->Move(R, Done);
	FString Frozen; FFileHelper::LoadFileToString(Frozen, *Journal->GetPath());
	for (auto Status : {ERPGItemBackendStatus::Unauthorized, ERPGItemBackendStatus::NotFound, ERPGItemBackendStatus::RevisionConflict})
	{
		F.Reply = Status; F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
		FString Current; FFileHelper::LoadFileToString(Current, *Journal->GetPath());
		TestTrue(TEXT("A negative recovery reply never resolves old uncertainty"), O.Stage == ERPGItemAsyncCommandStage::Uncertain && Frozen == Current);
	}
	F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	F.BeforeCommit = [&]() { IFileManager::Get().MakeDirectory(*(Journal->GetPath() + TEXT(".tmp"))); };
	F.Pipeline->RetryInventoryCommand(R.RequestId, F.Owner, Done);
	TestTrue(TEXT("Failure to finalize storage keeps the original and blocks future dispatch"),
		O.Stage == ERPGItemAsyncCommandStage::Uncertain && F.Pipeline->IsInventoryPersistenceBlocked() && F.Pipeline->HasUncertainInventoryCommands());
	TestTrue(TEXT("Cleanup failure preserves final evidence"), IFileManager::Get().FileExists(*Journal->GetPath()));
	{
		FJournalFiles RejectedFiles; FFixture Rejected; auto Clean = RejectedFiles.Open(); Clean->Restore();
		Rejected.Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(Rejected.Store, Rejected.Definitions, Rejected.Actions, Rejected, Rejected, Rejected, Clean);
		Rejected.Reply = ERPGItemBackendStatus::LocationConflict;
		Rejected.Pipeline->Move(Rejected.Move(), Done);
		auto Reopened = RejectedFiles.Open();
		TestTrue(TEXT("An initial correlated definite rejection can retire its write-ahead record"),
			O.Stage == ERPGItemAsyncCommandStage::BackendRejected && Reopened->Restore() && Reopened->GetPending().IsEmpty());
	}
	return true;
}
#endif
