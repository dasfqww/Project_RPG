#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Manager/DataManager.h"
#include "Engine/GameInstance.h"
#include "UObject/StrongObjectPtr.h"
#include "Item/Definition/RPGItemDefinition.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "UObject/CoreRedirects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryPluginMigrationTest,
    "ProjectRPG.Item.Plugin.ExistingAssetsAndRedirects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGInventoryPluginMigrationTest::RunTest(const FString&)
{
    TestEqual(TEXT("Definitions belong to the inventory module"),
        URPGItemDefinition::StaticClass()->GetOutermost()->GetName(), FString(TEXT("/Script/RPGInventoryRuntime")));
    TestEqual(TEXT("Redirect RPGItemBackendSubsystem"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemBackendSubsystem"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemBackendSubsystem")));
    TestEqual(TEXT("Redirect RPGItemDefinitionFragment"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemDefinitionFragment"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemDefinitionFragment")));
    TestEqual(TEXT("Redirect RPGItemDefinition"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemDefinition"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemDefinition")));
    TestEqual(TEXT("Redirect ERPGItemOwnerType"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.ERPGItemOwnerType"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.ERPGItemOwnerType")));
    TestEqual(TEXT("Redirect ERPGItemContainerType"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.ERPGItemContainerType"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.ERPGItemContainerType")));
    TestEqual(TEXT("Redirect ERPGItemBindState"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.ERPGItemBindState"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.ERPGItemBindState")));
    TestEqual(TEXT("Redirect ERPGItemLifecycleState"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.ERPGItemLifecycleState"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.ERPGItemLifecycleState")));
    TestEqual(TEXT("Redirect RPGItemOwnerRef"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemOwnerRef"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemOwnerRef")));
    TestEqual(TEXT("Redirect RPGItemLocation"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemLocation"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemLocation")));
    TestEqual(TEXT("Redirect RPGItemDurability"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemDurability"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemDurability")));
    TestEqual(TEXT("Redirect RPGItemRecordMetadata"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemRecordMetadata"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemRecordMetadata")));
    TestEqual(TEXT("Redirect RPGItemRecord"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemRecord"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemRecord")));
    TestEqual(TEXT("Redirect RPGItemStackTransfer"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemStackTransfer"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemStackTransfer")));
    TestEqual(TEXT("Redirect ERPGInventoryProjectionLoadState"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.ERPGInventoryProjectionLoadState"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.ERPGInventoryProjectionLoadState")));
    TestEqual(TEXT("Redirect RPGInventoryProjectionEntry"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGInventoryProjectionEntry"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGInventoryProjectionEntry")));
    TestEqual(TEXT("Redirect RPGInventoryProjectionList"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGInventoryProjectionList"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGInventoryProjectionList")));
    TestEqual(TEXT("Redirect RPGItemStatValue"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemStatValue"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemStatValue")));
    TestEqual(TEXT("Redirect RPGEquipmentProgressionState"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGEquipmentProgressionState"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGEquipmentProgressionState")));
    TestEqual(TEXT("Redirect RPGItemInstanceState"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Struct,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemInstanceState"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemInstanceState")));
    TestEqual(TEXT("Redirect RPGItemInstance"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemInstance"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemInstance")));
    TestEqual(TEXT("Redirect RPGItemFactory"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.RPGItemFactory"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.RPGItemFactory")));
    TestEqual(TEXT("Redirect EItemCategory"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.EItemCategory"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.EItemCategory")));
    TestEqual(TEXT("Redirect EEquipmentSlotType"),
        FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Enum,
            FCoreRedirectObjectName(TEXT("/Script/Project_RPG.EEquipmentSlotType"))).ToString(),
        FString(TEXT("/Script/RPGInventoryRuntime.EEquipmentSlotType")));
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
    TStrongObjectPtr<UDataManager> Catalog(NewObject<UDataManager>(Instance.Get()));
    Catalog->RefreshItemCache();
    const auto CheckExistingAsset = [this, &Catalog](const TCHAR* Path)
    {
        const auto* Definition = LoadObject<URPGItemDefinition>(nullptr, Path);
        if (TestNotNull(Path, Definition))
        {
            TestEqual(TEXT("Existing asset is discoverable without resaving"),
                Catalog->FindNativeItemDefinition(Definition->GetPrimaryAssetId()), Definition);
        }
    };
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_CirculationGreatSword.DA_Item_Progression_CirculationGreatSword"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_EnduranceGreatSword.DA_Item_Progression_EnduranceGreatSword"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_StarterChest.DA_Item_Progression_StarterChest"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_StarterGloves.DA_Item_Progression_StarterGloves"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_StarterGreatSword.DA_Item_Progression_StarterGreatSword"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_TargetChest.DA_Item_Progression_TargetChest"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_Progression_TargetGloves.DA_Item_Progression_TargetGloves"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_ProgressionOre.DA_Item_ProgressionOre"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Progression/DA_Item_ProgressionToken.DA_Item_ProgressionToken"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/V2/DA_Item_Helm_Leather.DA_Item_Helm_Leather"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/V2/DA_Item_Potion_Red_Large.DA_Item_Potion_Red_Large"));
    CheckExistingAsset(TEXT("/Game/Blueprints/GameData/Item/Weapons/DA_Item_GreatSword.DA_Item_GreatSword"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryPluginNotificationTest,
    "ProjectRPG.Item.Plugin.ProjectionNotification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGInventoryPluginNotificationTest::RunTest(const FString&)
{
    int32 Notifications = 0;
    FRPGInventoryProjectionList List(FSimpleDelegate::CreateLambda([&Notifications] { ++Notifications; }));
    FFastArraySerializer::FPostReplicatedReceiveParameters Parameters{};
    List.PostReplicatedReceive(Parameters);
    TestEqual(TEXT("Replicated data notifies the supplied adapter exactly once"), Notifications, 1);
    FRPGInventoryProjectionList Unbound;
    Unbound.PostReplicatedReceive(Parameters);
    TestEqual(TEXT("A standalone read model needs no game component"), Notifications, 1);
    return true;
}
#endif
