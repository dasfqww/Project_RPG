#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Item/Projection/RPGInventoryProjectionMapper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryProjectionCopyTest,
    "ProjectRPG.Item.Plugin.ProjectionCopyKeepsReceiver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGInventoryProjectionCopyTest::RunTest(const FString&)
{
    int32 TemplateNotifications = 0;
    int32 ReceiverNotifications = 0;
    FRPGInventoryProjectionList Template(FSimpleDelegate::CreateLambda(
        [&TemplateNotifications] { ++TemplateNotifications; }));
    FRPGInventoryProjectionList Receiver(FSimpleDelegate::CreateLambda(
        [&ReceiverNotifications] { ++ReceiverNotifications; }));
    const FGuid ItemId = FGuid::NewGuid();
    FRPGItemOwnerRef Owner;
    Owner.Type = ERPGItemOwnerType::Character;
    Owner.OwnerId = TEXT("copy-test-owner");
    FRPGItemLocation Location;
    Location.ContainerType = ERPGItemContainerType::Inventory;
    Location.ContainerId = Owner.OwnerId;
    Location.SlotIndex = 0;
    FRPGItemInstanceState State;
    FRPGItemRecord Record;
    TArray<FRPGInventoryProjectionEntry> Entries;
    bool bChanged = false;
    if (!TestTrue(TEXT("Restore payload"), FRPGItemInstanceState::TryRestore(ItemId, 0, 7, {}, {}, State)) ||
        !TestTrue(TEXT("Restore record"), FRPGItemRecord::TryRestore(
            FPrimaryAssetId(TEXT("RPGItemDefinition"), TEXT("CopyTest")), 1, Owner, Location,
            State, 1, ERPGItemLifecycleState::Active, {}, Record)) ||
        !TestTrue(TEXT("Map payload"), FRPGInventoryProjectionMapper::BuildInventorySnapshot(Owner, {Record}, Entries)) ||
        !TestTrue(TEXT("Populate template"), Template.Reconcile(Entries, bChanged))) return false;
    FRPGInventoryProjectionList::StaticStruct()->CopyScriptStruct(&Receiver, &Template);
    const auto* Copied = Receiver.Find(ItemId);
    if (TestNotNull(TEXT("Reflected copy transfers entries"), Copied))
        TestEqual(TEXT("Payload quantity preserved"), Copied->GetQuantity(), 7);
    FFastArraySerializer::FPostReplicatedReceiveParameters Parameters{};
    Receiver.PostReplicatedReceive(Parameters);
    TestEqual(TEXT("Reflected copy preserves the receiving component notification"), ReceiverNotifications, 1);
    TestEqual(TEXT("Reflected copy never notifies the template"), TemplateNotifications, 0);
    return true;
}
#endif
