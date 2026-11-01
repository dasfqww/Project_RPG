#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/RPGConsumeRecoveryTestEffect.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGPlayer.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Definition/RPGItemDefinitionCatalog.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Item/Policy/RPGItemActionPolicy.h"
#include "Item/Transaction/RPGInventoryCommandJournal.h"
#include "Player/RPGPlayerState.h"
#include "RPGItemTags.h"
#include "RPGGameplayTags.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGConsumeRecoveryTestAccess
{
	static bool Prepare(URPGItemCommandComponent& Component, const FRPGItemRepositoryCommitRequest& Request,
		const URPGItemDefinition* Definition, FString& Error)
	{ return Component.PrepareConsumeEffectFromDefinition(Request, Definition, Error); }
	static void Attach(URPGItemCommandComponent& Component, TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> Pipeline)
	{ Component.Orchestrator = MoveTemp(Pipeline); }
	static void Finish(URPGItemCommandComponent& Component, FGuid Id, const FRPGItemAsyncCommandOutcome& Outcome)
	{ Component.HandleOutcome(Id, TEXT("Item.Consume"), Outcome); }
	static bool HasTarget(const URPGItemCommandComponent& Component, FGuid Id) { return Component.ConsumeEffectTargets.Contains(Id); }
};

namespace RPGConsumeRecoveryTests
{
struct FFixture : IRPGItemAsyncCommitter, IRPGItemCommitSink, IRPGItemFirstCommitSink
{
	const FRPGItemOwnerRef Owner{ERPGItemOwnerType::Character, TEXT("11111111-2222-3333-4444-555555555555")};
	const FString Session = TEXT("11111111-2222-3333-4444-555555555556"), Steam = TEXT("76561198000000001");
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	ARPGPlayerState* State = nullptr;
	ARPGSkillDamageSecurityTestPawn* Pawn = nullptr;
	URPGItemCommandComponent* Commands = nullptr;
	URPGInventoryProjectionComponent* Projection = nullptr;
	URPGInventoryProjectionViewModel* Model = nullptr;
	URPGItemDefinition* Definition = nullptr;
	URPGItemConsumableDefinitionFragment* Fragment = nullptr;
	FRPGItemDefinitionRegistry Definitions;
	FRPGItemActionPolicyRegistry Policies;
	FRPGItemRecord Source;
	FRPGItemBackendCommitResult Saved;
	FRPGItemAsyncCommandOutcome Outcome;
	FRPGItemConsumeRequest Request;
	TSharedPtr<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe> Pipeline;
	ERPGItemBackendStatus Reply = ERPGItemBackendStatus::TransportError;
	TArray<FString> Bodies;
	TFunction<void(FRPGItemBackendCommitResult&)> Mutate;
	TFunction<void()> BeforeReply;
	int32 Writes = 0, EffectCalls = 0;
	bool bDefinitionReady = true;
	explicit FFixture(int32 Quantity = 7)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>(); State = World->SpawnActor<ARPGPlayerState>();
		if (!Controller || !State) return;
		Controller->SetPlayerState(State); State->SetOwner(Controller); State->SetAuthenticatedIdentity(Owner.OwnerId, Steam, Session);
		Pawn = MakePawn(); if (!Pawn) return;
		Controller->Possess(Pawn);
		Commands = NewObject<URPGItemCommandComponent>(Controller); Controller->AddInstanceComponent(Commands); Commands->RegisterComponent();
		Projection = NewObject<URPGInventoryProjectionComponent>(Controller); Controller->AddInstanceComponent(Projection); Projection->RegisterComponent();
		Definition = NewObject<URPGItemDefinition>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), URPGItemDefinition::StaticClass(), TEXT("ConsumeRecovery")));
		Definition->MaxStackSize = 10;
		Fragment = NewObject<URPGItemConsumableDefinitionFragment>(Definition);
		Fragment->GameplayEffect = URPGConsumeRecoveryTestEffect::StaticClass();
		Fragment->QuantityPerUse = 1; Fragment->SetByCallerMagnitudes.Add(RPGGameplayTags::Fragment_StatMod_1, 17.f);
		Definition->Fragments.Add(Fragment);
		Definitions.RegisterSnapshot({Definition->GetPrimaryAssetId(), 1, 10});
		FRPGItemActionPolicy Policy; Policy.DefinitionId = Definition->GetPrimaryAssetId(); Policy.DefinitionVersion = 1; Policy.QuantityPerUse = 1; Policies.RegisterPolicy(Policy);
		FRPGItemInstanceState Instance;
		FRPGItemInstanceState::TryRestore(FGuid::NewGuid(), 37, Quantity, {}, {}, Instance);
		FRPGItemRecord::TryRestore(Definition->GetPrimaryAssetId(), 1, Owner, {ERPGItemContainerType::Inventory, Owner.OwnerId, 0},
			Instance, 9007199254740993LL, ERPGItemLifecycleState::Active, {}, Source);
		Projection->ApplyAuthoritativeRecords(Owner, {Source});
		Projection->ApplyCompletedRewardSnapshot(Owner, {Source}, Projection->GetAuthoritativeSnapshotGeneration());
		Model = NewObject<URPGInventoryProjectionViewModel>(Controller); Model->InitializeFromPlayerController(Controller);
		Request.RequestId = FGuid::NewGuid(); Request.Actor = Owner; Request.ItemId = Source.GetItemId(); Request.ExpectedRevision = Source.GetRevision();
		Rebuild();
	}
	ARPGSkillDamageSecurityTestPawn* MakePawn()
	{
		auto* Result = World->SpawnActor<ARPGSkillDamageSecurityTestPawn>();
		if (Result)
		{
			Result->ASC()->InitAbilityActorInfo(Result, Result); Result->ASC()->AddAttributeSetSubobject(Result->Attributes());
			Result->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 200.f);
			Result->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 50.f);
		}
		return Result;
	}
	void Rebuild()
	{
		Pipeline = MakeShared<FRPGItemAsyncCommandOrchestrator, ESPMode::ThreadSafe>(*Projection->GetAuthoritativeRecordSource(), Definitions, Policies, *this, *this, *this);
		FRPGConsumeRecoveryTestAccess::Attach(*Commands, Pipeline);
	}
	~FFixture()
	{
		if (Model) Model->InitializeFromPlayerController(nullptr);
		if (Commands) FRPGConsumeRecoveryTestAccess::Attach(*Commands, {});
		Pipeline.Reset();
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	float Health() const { return Pawn->ASC()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
	void Finish(FRPGItemAsyncCommandOutcome Value)
	{ Outcome = MoveTemp(Value); FRPGConsumeRecoveryTestAccess::Finish(*Commands, Request.RequestId, Outcome); }
	bool Submit() { return Pipeline->Consume(Request, [this](FRPGItemAsyncCommandOutcome Value) { Finish(MoveTemp(Value)); }); }
	bool Retry() { return Pipeline->RetryInventoryCommand(Request.RequestId, Owner, [this](FRPGItemAsyncCommandOutcome Value) { Finish(MoveTemp(Value)); }); }
	bool SupportsScopedConsumeRecovery() const override { return true; }
	bool PrepareConsume(const FRPGItemRepositoryCommitRequest& Value, FString& Error) override
	{ return FRPGConsumeRecoveryTestAccess::Prepare(*Commands, Value, bDefinitionReady ? Definition : nullptr, Error); }
	void DiscardConsume(const FGuid& Id) override { Commands->DiscardConsumeEffect(Id); }
	bool EnqueueFirstCommit(const FRPGItemBackendCommitResult& Value, FString& Error) override
	{ ++EffectCalls; return Commands->EnqueueFirstCommitEffect(Value, Error); }
	bool ApplyCommit(const FRPGItemOwnerRef& Value, const FRPGItemBackendCommitResult& Result, FString& Error) override
	{ return Commands->ApplyCommitAndReconcile(Value, Result, Error); }
	bool Commit(const FRPGItemRepositoryCommitRequest& Value, FRPGItemBackendCommitCompletion Completion) override
	{
		FString Body; FRPGItemBackendJsonCodec::SerializeCommitRequest(Value, Body); Bodies.Add(Body);
		if (!Writes)
		{
			++Writes; Saved.RequestId = Value.RequestId; Saved.Operation = Value.Operation; Saved.Actor = Value.Actor;
			Saved.CommandFingerprint = Value.CommandFingerprint; Saved.AffectedQuantity = Value.AffectedQuantity;
			for (const auto& Mutation : Value.Mutations) Saved.Records.Add(Mutation.NewRecord.CopyWithRevision(Mutation.ExpectedRevision + 1));
		}
		auto Result = Saved; Result.Status = Reply;
		if (Mutate) Mutate(Result);
		if (BeforeReply) BeforeReply();
		Completion(MoveTemp(Result)); return true;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryFrozenTest,
	"ProjectRPG.Item.ConsumeRecovery.FrozenReplayDeliversOriginalHealOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryFrozenTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	for (const int32 Quantity : {1, 7})
	{
		FFixture F(Quantity); if (!TestNotNull(TEXT("Prepared fixture"), F.Pipeline.Get())) return false;
		F.Submit(); TestEqual(TEXT("Lost reply remains uncertain"), F.Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
		TestEqual(TEXT("No heal without a validated receipt"), F.Health(), 50.f);
		TestTrue(TEXT("Existing BP recovery enabled"), F.Model->bHasInventoryRecovery && F.Model->bCanRetryInventoryRecovery);
		TestTrue(TEXT("Frozen request has a server-created effect scope"), F.Saved.CommandFingerprint.Contains(TEXT("|effectScope=")));
		F.Reply = ERPGItemBackendStatus::Unauthorized; F.Retry();
		TestTrue(TEXT("Unauthorized retry retains original intent and UI lock"), F.Pipeline->HasUncertainInventoryCommands() && F.Model->bHasInventoryRecovery);
		F.Reply = ERPGItemBackendStatus::AlreadyApplied;
		F.Mutate = [](auto& Receipt) { ++Receipt.AffectedQuantity; }; F.Retry();
		TestEqual(TEXT("Changed receipt cannot heal"), F.Health(), 50.f);
		F.Mutate = {}; F.Retry();
		TestTrue(TEXT("Original exact receipt resolves"), F.Outcome.WasSuccessful());
		TestEqual(TEXT("Original effect delivered once"), F.Health(), 67.f);
		TestEqual(TEXT("Only one simulated durable item commit"), F.Writes, 1);
		for (const auto& Body : F.Bodies) TestEqual(TEXT("Every replay retains original bytes"), Body, F.Bodies[0]);
		TestFalse(TEXT("Validated receipt and effect release BP recovery"), F.Model->bHasInventoryRecovery);
		const int32 Sends = F.Bodies.Num(); F.Submit();
		TestTrue(TEXT("Completed replay is cached"), F.Outcome.WasSuccessful() && F.Bodies.Num() == Sends);
		TestEqual(TEXT("Cached replay cannot heal again"), F.Health(), 67.f);
		FRPGItemRecord Stored;
		TestTrue(TEXT("Terminal or active original identity retained"), F.Projection->GetAuthoritativeRecordSource()->Find(F.Source.GetItemId(), Stored));
		TestEqual(TEXT("Quantity reduced once"), Stored.GetQuantity(), Quantity - 1);
		TestEqual(TEXT("Exact int64 revision advanced once"), Stored.GetRevision(), F.Source.GetRevision() + 1);
	}
	TestFalse(TEXT("Transient combat effects are not replayed by the disk journal"), FRPGInventoryCommandJournal::Supports(TEXT("Item.Consume")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryDeliveryTest,
	"ProjectRPG.Item.ConsumeRecovery.BlockedEffectKeepsIntentAndFrozenMagnitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryDeliveryTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	F.Pawn->ASC()->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateLambda([](const auto&, const auto&) { return false; }));
	F.Reply = ERPGItemBackendStatus::Succeeded;
	F.BeforeReply = [&]() { F.Fragment->SetByCallerMagnitudes[RPGGameplayTags::Fragment_StatMod_1] = 99.f; };
	F.Submit();
	TestEqual(TEXT("A rejected GAS application does not claim effect success"), F.Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
	TestEqual(TEXT("Blocked effect did not heal"), F.Health(), 50.f);
	TestTrue(TEXT("Original prepared target survives"), FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
	F.Pawn->ASC()->GameplayEffectApplicationQueries.Reset(); F.BeforeReply = {}; F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	bool bReentrantRejected = false;
	const auto Handle = F.Pawn->ASC()->OnGameplayEffectAppliedDelegateToSelf.AddLambda([&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
	{
		auto Receipt = F.Saved; Receipt.Status = ERPGItemBackendStatus::AlreadyApplied; FString Error;
		bReentrantRejected = !F.Commands->EnqueueFirstCommitEffect(Receipt, Error);
	});
	F.Retry();
	F.Pawn->ASC()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
	TestTrue(TEXT("Retry delivers once and rejects re-entrant delivery"), F.Outcome.WasSuccessful() && bReentrantRejected);
	TestEqual(TEXT("Prepared magnitude, not a later definition edit"), F.Health(), 67.f);
	TestFalse(TEXT("Delivered target retired"), FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryAvatarTest,
	"ProjectRPG.Item.ConsumeRecovery.ReplacementAvatarCannotReceiveOldEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryAvatarTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	F.Submit(); auto* Replacement = F.MakePawn(); if (!Replacement) return false;
	F.Controller->Possess(Replacement); F.Commands->HandlePawnChanged(); F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.Retry();
	TestEqual(TEXT("A changed avatar leaves explicit unresolved recovery"), F.Outcome.Stage, ERPGItemAsyncCommandStage::Uncertain);
	TestEqual(TEXT("Original pawn was not healed"), F.Health(), 50.f);
	TestEqual(TEXT("Replacement pawn was not healed"), Replacement->ASC()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), 50.f);
	TestTrue(TEXT("Original recovery is not silently discarded"), F.Model->bHasInventoryRecovery);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryScopeTest,
	"ProjectRPG.Item.ConsumeRecovery.NewPipelineCannotReusePreviousEffectScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryScopeTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	F.Submit(); const FString OldBody = F.Bodies[0];
	F.Commands->DiscardConsumeEffect(F.Request.RequestId); F.Rebuild(); F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.Submit();
	TestTrue(TEXT("New server-side scope changes the fingerprint even with the same stale source/request"), F.Bodies.Num() == 2 && F.Bodies[1] != OldBody);
	TestEqual(TEXT("Old receipt cannot grant a new effect"), F.Health(), 50.f);
	TestEqual(TEXT("Old scope rejected before effect sink"), F.EffectCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryPreparationTest,
	"ProjectRPG.Item.ConsumeRecovery.PreparationFailureAndRevocationDoNotConsumeOrHeal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryPreparationTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	F.bDefinitionReady = false;
	TestFalse(TEXT("No commit without an effect definition"), F.Submit());
	TestEqual(TEXT("Preparation failure is not an unknown commit"), F.Outcome.Stage, ERPGItemAsyncCommandStage::PreparationRejected);
	TestEqual(TEXT("No backend write"), F.Writes, 0);
	TestFalse(TEXT("No fictitious recovery after pre-HTTP failure"), F.Model->bHasInventoryRecovery);
	F.bDefinitionReady = true; F.Submit();
	TestTrue(TEXT("Original target exists before revocation"), FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
	TestTrue(TEXT("Production identity revocation"), F.State->RevokeAuthenticatedIdentity(F.Session, F.Owner.OwnerId, F.Steam));
	TestFalse(TEXT("Revocation discards local effect target"), FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
	F.Commands->HandlePawnChanged(); auto Receipt = F.Saved; Receipt.Status = ERPGItemBackendStatus::AlreadyApplied; FString Error;
	TestFalse(TEXT("Late receipt cannot heal a revoked identity"), F.Commands->EnqueueFirstCommitEffect(Receipt, Error));
	TestEqual(TEXT("No heal after revocation"), F.Health(), 50.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryIncapacitatedPreparationTest,
	"ProjectRPG.Item.ConsumeRecovery.IncapacitatedBeforeCommitDoesNotConsume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryIncapacitatedPreparationTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	for (const bool bDeathTag : {false, true})
	{
		FFixture F; if (!F.Pipeline) return false;
		if (bDeathTag) F.Pawn->ASC()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		else F.Pawn->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		TestFalse(TEXT("Incapacitated avatar cannot begin an ordinary consume"), F.Submit());
		TestEqual(TEXT("Known pre-HTTP refusal"), F.Outcome.Stage, ERPGItemAsyncCommandStage::PreparationRejected);
		TestEqual(TEXT("No backend commit"), F.Writes, 0);
		TestEqual(TEXT("No body was sent"), F.Bodies.Num(), 0);
		TestFalse(TEXT("No fake recovery or prepared target"), F.Model->bHasInventoryRecovery ||
			FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
		FRPGItemRecord Current;
		TestTrue(TEXT("Original item still present"), F.Projection->GetAuthoritativeRecordSource()->Find(F.Source.GetItemId(), Current));
		TestEqual(TEXT("Original quantity retained"), Current.GetQuantity(), F.Source.GetQuantity());
		TestEqual(TEXT("Original int64 revision retained"), Current.GetRevision(), F.Source.GetRevision());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryDeathDuringCommitTest,
	"ProjectRPG.Item.ConsumeRecovery.DeathBeforeReceiptCannotHeal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryDeathDuringCommitTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	for (const int32 Quantity : {1, 7})
	for (const bool bDeathTag : {false, true})
	for (const bool bFirstReceipt : {false, true})
	{
		FFixture F(Quantity); if (!F.Pipeline) return false;
		auto Incapacitate = [&]()
		{
			if (bDeathTag) F.Pawn->ASC()->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
			else F.Pawn->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
		};
		if (bFirstReceipt) { F.Reply = ERPGItemBackendStatus::Succeeded; F.BeforeReply = Incapacitate; }
		F.Submit();
		if (!bFirstReceipt) { Incapacitate(); F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.Retry(); }
		TestEqual(TEXT("Neither first nor replayed receipt heals"), F.Health(), bDeathTag ? 50.f : 0.f);
		TestTrue(TEXT("Validated consumption completes without delivering the expired effect"), F.Outcome.WasSuccessful());
		TestFalse(TEXT("Completed consumption retires prepared target and BP recovery"),
			FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId) || F.Model->bHasInventoryRecovery ||
			F.Pipeline->HasUncertainInventoryCommands());
		TestEqual(TEXT("No extra durable consumption"), F.Writes, 1);
		for (const auto& Body : F.Bodies) TestEqual(TEXT("No new body or request during recovery"), Body, F.Bodies[0]);
		FRPGItemRecord Current;
		TestTrue(TEXT("Confirmed original item retained"), F.Projection->GetAuthoritativeRecordSource()->Find(F.Source.GetItemId(), Current));
		TestEqual(TEXT("Quantity consumed once without refund"), Current.GetQuantity(), Quantity - 1);
		TestEqual(TEXT("Exact revision advanced once"), Current.GetRevision(), F.Source.GetRevision() + 1);
		F.BeforeReply = {}; F.Pawn->ASC()->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death);
		F.Pawn->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 50.f);
		const int32 Sends = F.Bodies.Num(); F.Submit();
		TestTrue(TEXT("Expired command is completed from cache without HTTP"), F.Outcome.WasSuccessful() && F.Bodies.Num() == Sends);
		TestEqual(TEXT("Recovery of health cannot replay the expired effect"), F.Health(), 50.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryUnconfirmedDeathTest,
	"ProjectRPG.Item.ConsumeRecovery.UnconfirmedDeathRetainsRecoveryUntilExactReceipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryUnconfirmedDeathTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	F.Submit();
	F.Pawn->ASC()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	for (const auto Status : {ERPGItemBackendStatus::TransportError, ERPGItemBackendStatus::Unauthorized,
		ERPGItemBackendStatus::NotFound, ERPGItemBackendStatus::RevisionConflict})
	{
		F.Reply = Status; F.Retry();
		TestTrue(TEXT("Death cannot resolve a missing or refused receipt"), F.Pipeline->HasUncertainInventoryCommands() &&
			F.Model->bHasInventoryRecovery && FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId));
	}
	F.Reply = ERPGItemBackendStatus::AlreadyApplied;
	F.Mutate = [](auto& Receipt) { ++Receipt.AffectedQuantity; }; F.Retry();
	TestTrue(TEXT("Death cannot accept a changed receipt"), F.Pipeline->HasUncertainInventoryCommands() && F.Model->bHasInventoryRecovery);
	F.Mutate = {}; F.Retry();
	TestTrue(TEXT("Only the validated original receipt completes"), F.Outcome.WasSuccessful());
	TestFalse(TEXT("Existing BP recovery lock released"), F.Model->bHasInventoryRecovery || F.Pipeline->HasUncertainInventoryCommands());
	TestEqual(TEXT("No resurrection or refund"), F.Health(), 0.f);
	TestEqual(TEXT("Only original durable write"), F.Writes, 1);
	for (const auto& Body : F.Bodies) TestEqual(TEXT("All retries use the frozen original request"), Body, F.Bodies[0]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGConsumeRecoveryNewLifeTest,
	"ProjectRPG.Item.ConsumeRecovery.SamePawnNewEncounterCannotReceiveOldEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGConsumeRecoveryNewLifeTest::RunTest(const FString&)
{
	using namespace RPGConsumeRecoveryTests;
	FFixture F; if (!F.Pipeline) return false;
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Player = F.World->SpawnActor<ARPGPlayer>(ARPGPlayer::StaticClass(), FTransform::Identity, Params);
	if (!TestNotNull(TEXT("Native player"), Player)) return false;
	auto* StartupProperty = FindFProperty<FSoftObjectProperty>(Player->GetClass(), TEXT("CharacterStartUpData"));
	if (!TestNotNull(TEXT("Existing startup property"), StartupProperty)) return false;
	TStrongObjectPtr<UDataAsset_StartUpDataBase> Startup(NewObject<UDataAsset_StartUpDataBase>(Player));
	StartupProperty->SetPropertyValue_InContainer(Player, FSoftObjectPtr(Startup.Get()));
	F.Controller->Possess(Player);
	auto* ASC = Player->GetRPGAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Player, Player);
	if (!ASC->GetSet<URPGAttributeSet>()) ASC->AddAttributeSetSubobject(Player->GetRPGAttributeSet());
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 200.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 50.f);
	F.Submit();
	// Fixture-only advancement of the existing server recovery field. This is not
	// a real defeat/reset workflow and does not relax its production preconditions.
	auto* Revision = FindFProperty<FUInt32Property>(Player->GetClass(), TEXT("EncounterRecoveryRevision"));
	if (!TestNotNull(TEXT("Existing recovery generation"), Revision)) return false;
	Revision->SetPropertyValue_InContainer(Player, Player->GetCombatPresentationRevision() + 1);
	F.Reply = ERPGItemBackendStatus::AlreadyApplied; F.Retry();
	TestEqual(TEXT("Same Pawn/ASC in a new encounter cannot receive an old heal"),
		ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), 50.f);
	TestTrue(TEXT("Original committed request completes without refund/reissue"), F.Outcome.WasSuccessful());
	TestEqual(TEXT("Only original durable consumption"), F.Writes, 1);
	TestFalse(TEXT("Expired effect and BP recovery retired"), FRPGConsumeRecoveryTestAccess::HasTarget(*F.Commands, F.Request.RequestId) ||
		F.Model->bHasInventoryRecovery || F.Pipeline->HasUncertainInventoryCommands());
	const int32 Sends = F.Bodies.Num(); F.Submit();
	TestTrue(TEXT("New encounter replay is cached without a new request"), F.Outcome.WasSuccessful() && F.Bodies.Num() == Sends);
	TestEqual(TEXT("New encounter cannot receive a cached old effect"), ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()), 50.f);
	return true;
}
#endif
