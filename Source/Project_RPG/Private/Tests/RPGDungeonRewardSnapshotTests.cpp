#if WITH_DEV_AUTOMATION_TESTS

#include "Component/RPGInventoryProjectionComponent.h"
#include "Economy/RPGDungeonRewardSnapshot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Misc/AutomationTest.h"
#include "Player/RPGPlayerState.h"
#include "Serialization/JsonSerializer.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"

namespace RPGRewardSnapshotTests
{
const FString Character = TEXT("10203040-5060-7080-90a0-b0c0d0e0f001");
const FString Session = TEXT("10203040-5060-7080-90a0-b0c0d0e0f002");
const FString Roster = TEXT("10203040-5060-7080-90a0-b0c0d0e0f003");
FRPGItemOwnerRef Owner()
{
	FRPGItemOwnerRef Result;
	Result.Type = ERPGItemOwnerType::Character;
	Result.OwnerId = Character;
	return Result;
}
FRPGItemRecord Record(FGuid Id, int32 Quantity, int64 Revision, int32 Slot = 0)
{
	FRPGItemInstanceState State;
	FRPGItemInstanceState::TryRestore(Id, 42, Quantity, {}, {}, State);
	FRPGItemLocation Location;
	Location.ContainerType = Quantity > 0 ? ERPGItemContainerType::Inventory : ERPGItemContainerType::Terminal;
	Location.ContainerId = Quantity > 0 ? Character : FString();
	Location.SlotIndex = Quantity > 0 ? Slot : INDEX_NONE;
	FRPGItemRecord Result;
	FRPGItemRecord::TryRestore(FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("SnapshotPotion")), 1,
		Owner(), Location, State, Revision,
		Quantity > 0 ? ERPGItemLifecycleState::Active : ERPGItemLifecycleState::Consumed, {}, Result);
	return Result;
}
TSharedRef<FJsonObject> Ready(const FRPGItemRecord& Item)
{
	FRPGItemRepositoryCommitRequest Commit;
	Commit.RequestId = FGuid::NewGuid();
	Commit.Operation = TEXT("RewardTest");
	Commit.CommandFingerprint = TEXT("snapshot-test");
	Commit.Actor = Owner();
	Commit.AffectedQuantity = Item.GetQuantity();
	FRPGItemRecordMutation Mutation;
	Mutation.ExpectedRevision = Item.GetRevision();
	Mutation.NewRecord = Item;
	Commit.Mutations.Add(Mutation);
	FString CommitJson;
	if (!FRPGItemBackendJsonCodec::SerializeCommitRequest(Commit, CommitJson)) return MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> CommitObject;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CommitJson), CommitObject,
		FJsonSerializer::EFlags::StoreNumbersAsStrings);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("dungeonSessionId"), Session);
	Root->SetStringField(TEXT("characterId"), Character);
	Root->SetStringField(TEXT("state"), TEXT("Ready"));
	Root->SetArrayField(TEXT("items"), { MakeShared<FJsonValueObject>(
		CommitObject->GetArrayField(TEXT("mutations"))[0]->AsObject()->GetObjectField(TEXT("newRecord"))) });
	TSharedPtr<FJsonObject> Wallet;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString::Printf(
		TEXT("{\"characterId\":\"%s\",\"rosterId\":\"%s\",\"accountId\":\"steam-test\",\"balances\":[")
		TEXT("{\"currencyCode\":\"TestGold\",\"displayName\":\"Test Gold\",\"scope\":\"Roster\",\"ownerId\":\"%s\",\"balance\":9007199254740993,\"maxBalance\":9223372036854775807,\"revision\":9007199254740993}]}"),
		*Character, *Roster, *Roster)), Wallet, FJsonSerializer::EFlags::StoreNumbersAsStrings);
	Root->SetObjectField(TEXT("wallet"), Wallet);
	return Root;
}
FString Json(const TSharedRef<FJsonObject>& Root)
{
	FString Result;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Result));
	return Result;
}
struct FWorldFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	FWorldFixture() { if (World) GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FWorldFixture()
	{
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSnapshotCodecTest, "ProjectRPG.Economy.RewardSnapshot.Codec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSnapshotCodecTest::RunTest(const FString&)
{
	using namespace RPGRewardSnapshotTests;
	auto Root = Ready(Record(FGuid::NewGuid(), 3, 9007199254740993LL));
	FRPGDungeonRewardSnapshotResult Result;
	TestTrue(TEXT("Ready wallet and active items decode"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	if (!TestEqual(TEXT("One balance"), Result.Wallet.Balances.Num(), 1) || !TestEqual(TEXT("One item"), Result.Items.Num(), 1)) return false;
	TestEqual(TEXT("Nested wallet retains int64 precision"), Result.Wallet.Balances[0].Balance, 9007199254740993LL);
	TestEqual(TEXT("Item revision retains int64 precision"), Result.Items[0].GetRevision(), 9007199254740993LL);
	TestFalse(TEXT("Legacy snapshot does not invent a receipt count"), Result.DeliveredItemQuantity.IsSet());
	auto Delivery = MakeShared<FJsonObject>();
	Delivery->SetStringField(TEXT("rewardVersion"), TEXT("personal_loot_v1"));
	Root->SetObjectField(TEXT("delivery"), Delivery);
	for (int32 Quantity : { 0, 6, 7, MAX_int32 })
	{
		Delivery->SetNumberField(TEXT("itemQuantity"), Quantity);
		if (!TestTrue(TEXT("Actual receipt quantity decodes including all-miss zero"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result))) return false;
		TestTrue(TEXT("Count stays exact"), Result.DeliveredItemQuantity.IsSet() && Result.DeliveredItemQuantity.GetValue() == Quantity);
		TestEqual(TEXT("Receipt version is retained for GameMode matching"), Result.DeliveryRewardVersion, FString(TEXT("personal_loot_v1")));
	}
	for (double Invalid : { -1.0, 0.5, 2147483648.0 })
	{
		Delivery->SetNumberField(TEXT("itemQuantity"), Invalid);
		TestFalse(TEXT("Invalid receipt count is rejected"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	}
	Delivery->SetStringField(TEXT("itemQuantity"), TEXT("7"));
	TestFalse(TEXT("Quoted numeric receipt count is rejected"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	Root->RemoveField(TEXT("delivery"));
	TestFalse(TEXT("Other session rejected"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, FGuid::NewGuid().ToString(), Character, Result));
	TestFalse(TEXT("Other member rejected"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, FGuid::NewGuid().ToString(), Result));
	Root->GetObjectField(TEXT("wallet"))->SetStringField(TEXT("characterId"), FGuid::NewGuid().ToString());
	TestFalse(TEXT("Wallet identity cannot differ"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	Root = Ready(Record(FGuid::NewGuid(), 3, 1));
	Root->GetArrayField(TEXT("items"))[0]->AsObject()->GetObjectField(TEXT("owner"))->SetStringField(TEXT("ownerId"), FGuid::NewGuid().ToString());
	TestFalse(TEXT("Foreign item rejected"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	Root = Ready(Record(FGuid::NewGuid(), 3, 1));
	Root->RemoveField(TEXT("wallet"));
	Root->RemoveField(TEXT("items"));
	Root->SetStringField(TEXT("state"), TEXT("Pending"));
	TestTrue(TEXT("Pending is retryable without a partial wallet"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 202, Session, Character, Result));
	TestEqual(TEXT("Pending status"), Result.Status, ERPGDungeonRewardSnapshotStatus::Pending);
	TestFalse(TEXT("Pending cannot masquerade as Ready"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Root), 200, Session, Character, Result));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSnapshotGenerationTest, "ProjectRPG.Economy.RewardSnapshot.Generation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSnapshotGenerationTest::RunTest(const FString&)
{
	using namespace RPGRewardSnapshotTests;
	FWorldFixture Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	auto* Controller = Fixture.World->SpawnActor<APlayerController>();
	auto* Projection = NewObject<URPGInventoryProjectionComponent>(Controller);
	Controller->AddInstanceComponent(Projection);
	Projection->RegisterComponent();
	const FGuid ItemId = FGuid::NewGuid(), TerminalId = FGuid::NewGuid();
	const auto Original = Record(ItemId, 3, 1);
	const auto Latest = Record(ItemId, 2, 2);
	const auto Terminal = Record(TerminalId, 0, 2);
	if (!TestTrue(TEXT("Initial authoritative cache"), Projection->ApplyAuthoritativeRecords(Owner(), { Original, Terminal }))) return false;
	const uint64 Generation = Projection->GetAuthoritativeSnapshotGeneration();
	TestTrue(TEXT("Command result changes cache generation"), Projection->ApplyAuthoritativeMutationRecords(Owner(), { Latest }));
	TestFalse(TEXT("Late snapshot cannot undo a command"), Projection->ApplyCompletedRewardSnapshot(Owner(), { Original }, Generation));
	FRPGItemRecord Cached;
	Projection->GetAuthoritativeRecordSource()->Find(ItemId, Cached);
	TestEqual(TEXT("New quantity retained"), Cached.GetQuantity(), 2);
	TestTrue(TEXT("Current generation accepts full active snapshot"), Projection->ApplyCompletedRewardSnapshot(Owner(), { Latest }, Projection->GetAuthoritativeSnapshotGeneration()));
	TestTrue(TEXT("Known terminal record retained for idempotency"), Projection->GetAuthoritativeRecordSource()->Find(TerminalId, Cached));
	TestEqual(TEXT("Terminal lifecycle retained"), Cached.GetLifecycleState(), ERPGItemLifecycleState::Consumed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSnapshotWalletTest, "ProjectRPG.Economy.RewardSnapshot.WalletAndNotice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSnapshotWalletTest::RunTest(const FString&)
{
	using namespace RPGRewardSnapshotTests;
	FWorldFixture Fixture;
	if (!TestNotNull(TEXT("World"), Fixture.World)) return false;
	auto* State = Fixture.World->SpawnActor<ARPGPlayerState>();
	State->SetAuthenticatedIdentity(Character, TEXT("steam-test"), Session);
	FRPGDungeonRewardSnapshotResult Result;
	if (!TestTrue(TEXT("Decode wallet fixture"), FRPGDungeonRewardSnapshotCodec::Decode(Json(Ready(Record(FGuid::NewGuid(), 3, 1))), 200, Session, Character, Result))) return false;
	TestFalse(TEXT("Foreign session wallet rejected"), State->CanApplyCompletedRewardWallet(Result.Wallet, FGuid::NewGuid().ToString()));
	auto Foreign = Result.Wallet;
	Foreign.AccountId = TEXT("another-account");
	TestFalse(TEXT("Foreign account wallet rejected"), State->CanApplyCompletedRewardWallet(Foreign, Session));
	Foreign = Result.Wallet;
	Foreign.Balances[0].OwnerId = TEXT("another-roster");
	TestFalse(TEXT("Foreign balance owner rejected"), State->CanApplyCompletedRewardWallet(Foreign, Session));
	FRPGDungeonRewardDeliveryNotice Notice;
	Notice.DungeonSessionId = Session;
	Notice.RewardVersion = TEXT("test_v1");
	Notice.InventoryQuantity = 3;
	Notice.MailQuantity = 1;
	Notice.bApplied = true;
	TestTrue(TEXT("Validated wallet and delivery notice applied"), State->ApplyCompletedRewardWallet(Result.Wallet, Notice));
	TestEqual(TEXT("Wallet updated"), State->GetCurrencyBalance(TEXT("TestGold")), 9007199254740993LL);
	TestTrue(TEXT("Owner notice available to late-opening UI"), State->GetRewardDeliveryNotice().bApplied);
	auto* Controller = Fixture.World->SpawnActor<APlayerController>();
	Controller->SetPlayerState(State);
	auto* Model = NewObject<URPGInventoryProjectionViewModel>(Controller);
	Model->InitializeFromPlayerController(Controller);
	TestFalse(TEXT("Late-opening ViewModel restores the delivery message"), Model->RewardDeliveryMessage.IsEmpty());
	State->SetAuthenticatedIdentity(Character, TEXT("steam-test"), FGuid::NewGuid().ToString());
	TestFalse(TEXT("New dungeon resets previous notice"), State->GetRewardDeliveryNotice().bApplied);
	TestTrue(TEXT("Already-open ViewModel clears the old dungeon message"), Model->RewardDeliveryMessage.IsEmpty());
	Model->InitializeFromPlayerController(nullptr);
	return true;
}
#endif
