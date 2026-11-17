#if WITH_DEV_AUTOMATION_TESTS
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Settings/RPGGameUserSettings.h"
#include "UI/ItemQuickSlot/RPGItemQuickSlotBar.h"
#include "UI/RPGQuickSlotWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemQuickSlotDefaultsTest,
    "ProjectRPG.Item.QuickSlot.ExpandedDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemQuickSlotDefaultsTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDataAsset_InputConfig> Config(NewObject<UDataAsset_InputConfig>(GetTransientPackage(), NAME_None, RF_Transient));
    Config->DefaultMappingContext = NewObject<UInputMappingContext>(Config.Get(), NAME_None, RF_Transient);
    auto* Skill = NewObject<UInputAction>(Config.Get());
    FWarriorInputActionConfig Row;
    Row.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.QuickSkill.1")); Row.InputAction = Skill;
    Config->NativeInputActions.Add(Row); Config->DefaultMappingContext->MapKey(Skill, EKeys::Q);
    FRPGQuickSlotInputBindings::ConfigureItemDefaults(*Config);
    const FKey Keys[] = {EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::X};
    auto Validate = [&]
    {
        const auto Bindings = FRPGQuickSlotInputBindings::Read(Config.Get());
        TestEqual(TEXT("Nine items and unchanged skill; hidden F1 alias binds only once"), Bindings.Num(), 10);
        for (int32 Index = 0; Index < 9; ++Index)
        {
            const auto* Binding = Bindings.FindByPredicate([Index](const auto& B) { return !B.bIsSkillSlot && B.SlotIndex == Index; });
            if (!TestNotNull(TEXT("Every visible item slot is bound"), Binding)) continue;
            int32 Matches = 0;
            for (const auto& Mapping : Config->DefaultMappingContext->GetMappings())
                if (Mapping.Action == Binding->Action) { ++Matches; TestEqual(TEXT("Expected item key"), Mapping.Key, Keys[Index]); }
            TestEqual(TEXT("One physical default per item action"), Matches, 1);
        }
        TestEqual(TEXT("No duplicate default key records"), Config->DefaultMappingContext->GetMappings().Num(), 10);
        TestEqual(TEXT("Skill key preserved"), Config->DefaultMappingContext->GetMappings()[0].Key, EKeys::Q);
    };
    Validate();
    FRPGQuickSlotInputBindings::ConfigureItemDefaults(*Config); Validate();
    TStrongObjectPtr<UDataAsset_InputConfig> Old(NewObject<UDataAsset_InputConfig>());
    Old->DefaultMappingContext = NewObject<UInputMappingContext>(Old.Get());
    auto* PreviousAction = NewObject<UInputAction>(Old.Get());
    FWarriorInputActionConfig PreviousRow;
    PreviousRow.InputTag = FGameplayTag::RequestGameplayTag(TEXT("InputTag.QuickItem.1")); PreviousRow.InputAction = PreviousAction;
    Old->NativeInputActions.Add(PreviousRow); Old->DefaultMappingContext->MapKey(PreviousAction, EKeys::One);
    FRPGKeyMapping SavedRow; SavedRow.InputTag = PreviousRow.InputTag; SavedRow.DefaultKey = EKeys::One; SavedRow.Key = EKeys::Z;
    TArray<FRPGKeyMapping> Saved{SavedRow};
    FRPGQuickSlotInputBindings::MigrateSavedItemBindings(*Old, *Config, Saved);
    TestEqual(TEXT("Numeric default is rebased to F1"), Saved[0].DefaultKey, EKeys::F1);
    TestEqual(TEXT("User's chosen key survives rebasing"), Saved[0].Key, EKeys::Z);
    FRPGKeyMapping NewRow = Saved[0]; NewRow.Key = EKeys::V;
    Saved = {SavedRow, NewRow};
    FRPGQuickSlotInputBindings::MigrateSavedItemBindings(*Old, *Config, Saved);
    TestEqual(TEXT("An explicit new-default setting wins"), Saved.Num(), 1);
    TestEqual(TEXT("New setting is never overwritten by old numeric setting"), Saved[0].Key, EKeys::V);
    TestEqual(TEXT("Legacy F1 alias resolves canonical first item"),
        Config->FindNativeInputActionByTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.QuickItem.F1"))),
        Config->FindNativeInputActionByTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.QuickItem.1"))));
    TStrongObjectPtr<UDataAsset_InputConfig> Asset(NewObject<UDataAsset_InputConfig>());
    Asset->DefaultMappingContext = NewObject<UInputMappingContext>(Asset.Get());
    FRPGQuickSlotInputBindings::ConfigureItemDefaults(*Asset);
    TestEqual(TEXT("Never edits authored defaults in place"), Asset->NativeInputActions.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPotionAutoUseDecisionTest,
    "ProjectRPG.Item.QuickSlot.AutoRecoveryThreshold", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPotionAutoUseDecisionTest::RunTest(const FString&)
{
    TestTrue(TEXT("Inclusive configured threshold"), UQuickSlotComponent::ShouldAutoUsePotion(50.f, 100.f, 50));
    TestFalse(TEXT("Healthy avatars do not consume"), UQuickSlotComponent::ShouldAutoUsePotion(51.f, 100.f, 50));
    TestTrue(TEXT("25 percent setting"), UQuickSlotComponent::ShouldAutoUsePotion(25.f, 100.f, 25));
    TestTrue(TEXT("75 percent setting"), UQuickSlotComponent::ShouldAutoUsePotion(75.f, 100.f, 75));
    TestFalse(TEXT("Dead avatar never consumes"), UQuickSlotComponent::ShouldAutoUsePotion(0.f, 100.f, 50));
    TestFalse(TEXT("Uninitialized attributes never consume"), UQuickSlotComponent::ShouldAutoUsePotion(1.f, 0.f, 50));
    TestFalse(TEXT("Nonfinite health never consumes"), UQuickSlotComponent::ShouldAutoUsePotion(std::numeric_limits<float>::quiet_NaN(), 100.f, 50));
    TestFalse(TEXT("Invalid setting never consumes"), UQuickSlotComponent::ShouldAutoUsePotion(1.f, 100.f, 100));
    TStrongObjectPtr<UQuickSlotComponent> Slots(NewObject<UQuickSlotComponent>());
    TestEqual(TEXT("Nine slots exist even before BeginPlay"), Slots->GetMaxItemSlots(), 9);
    TestFalse(TEXT("Auto use starts disabled"), Slots->IsPotionAutoUseEnabled());
    Slots->SetPotionAutoUse(true); TestFalse(TEXT("Unowned or empty potion cannot enable automation"), Slots->IsPotionAutoUseEnabled());
    Slots->CyclePotionHealthThreshold(); TestEqual(TEXT("50 to 75"), Slots->GetPotionHealthThreshold(), 75);
    Slots->CyclePotionHealthThreshold(); TestEqual(TEXT("75 to 25"), Slots->GetPotionHealthThreshold(), 25);
    Slots->CyclePotionHealthThreshold(); TestEqual(TEXT("25 to 50"), Slots->GetPotionHealthThreshold(), 50);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGItemQuickSlotNativeWidgetTest,
    "ProjectRPG.Item.QuickSlot.NativeRightBar", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGItemQuickSlotNativeWidgetTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!World) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* PC = World->SpawnActor<ARPGPlayerController>();
    auto* Pawn = World->SpawnActor<APawn>();
    if (!PC || !Pawn) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto* Local = NewObject<ULocalPlayer>(GEngine);
    PC->Player = Local; Local->PlayerController = PC; PC->SetAsLocalPlayerController(); World->AddController(PC); PC->Possess(Pawn);
    auto* Slots = NewObject<UQuickSlotComponent>(Pawn); Pawn->AddInstanceComponent(Slots); Slots->RegisterComponent();
    TStrongObjectPtr<URPGItemQuickSlotBar> Bar(CreateWidget<URPGItemQuickSlotBar>(PC));
    if (TestNotNull(TEXT("Right item bar"), Bar.Get()))
    {
        auto Slate = Bar->TakeWidget();
        TestEqual(TEXT("All nine native cells are visible"), Bar->GetCellCount(), 9);
        for (int32 Index = 0; Index < Bar->GetCellCount(); ++Index)
        {
            auto* Cell = Bar->GetCell(Index);
            TestNotNull(TEXT("Native status overlay"), Cell->GetWidgetFromName(TEXT("ItemStatusText")));
            TestFalse(TEXT("No legacy item instance"), Cell->GetSlotItem() != nullptr);
            TestTrue(TEXT("Registration help on empty cells"), !Cell->GetToolTipText().IsEmpty());
        }
        TestNull(TEXT("Invalid cell lookup"), Bar->GetCell(9));
        Bar->ReleaseSlateResources(true);
    }
    Bar.Reset(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
