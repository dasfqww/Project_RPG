#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/RPGActionBarLayout.h"
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "InputCoreTypes.h"

namespace RPGActionBarLayoutTests
{
TSharedPtr<SWidget> FindButton(const TSharedRef<SWidget>& Root)
{
    Root->UpdateAllAttributes();
    if (Root->GetType() == TEXT("SButton")) return Root;
    auto* Children = Root->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Found = FindButton(Children->GetChildAt(I))) return Found;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarLayoutValidation, "ProjectRPG.UI.ActionBar.Layout.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarLayoutValidation::RunTest(const FString&)
{
    TArray<int32> Order{42};
    auto Resolve = [&Order](TArray<int32> Slots, int32 Count) { return RPGActionBarLayout::Resolve(Slots, Count, Order); };
    TestFalse(TEXT("Duplicate explicit positions rejected"), Resolve({1, 1}, 4));
    TestTrue(TEXT("Failure clears previous output"), Order.IsEmpty());
    TestFalse(TEXT("Negative explicit position rejected"), Resolve({-2}, 0));
    TestFalse(TEXT("Position beyond eight rejected"), Resolve({8}, 0));
    TestFalse(TEXT("Negative fixed count rejected"), Resolve({}, -1));
    TestFalse(TEXT("Fixed count beyond eight rejected"), Resolve({}, 9));
    TestFalse(TEXT("Position outside fixed row rejected"), Resolve({3}, 3));
    TestFalse(TEXT("Too many actions for fixed row rejected"), Resolve({-1, -1}, 1));
    TestFalse(TEXT("More than eight actions rejected"), Resolve({-1, -1, -1, -1, -1, -1, -1, -1, -1}, 0));
    TestTrue(TEXT("Empty central-only profile remains valid"), Resolve({}, 0) && Order.IsEmpty());
    TestTrue(TEXT("Maximum explicit position infers eight slots"), Resolve({7}, 0) && Order.Num() == 8 && Order[7] == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarLayoutCompatibility, "ProjectRPG.UI.ActionBar.Layout.Compatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarLayoutCompatibility::RunTest(const FString&)
{
    TArray<int32> Order;
    const TArray<int32> OldProfile{-1, -1, -1};
    TestTrue(TEXT("Old profiles keep compact action order"), RPGActionBarLayout::Resolve(OldProfile, 0, Order)
        && Order == TArray<int32>{0, 1, 2});
    const TArray<int32> Mixed{-1, 0, 3, -1};
    TestTrue(TEXT("Explicit positions reserved before filling automatic actions"), RPGActionBarLayout::Resolve(Mixed, 0, Order)
        && Order == TArray<int32>{1, 0, 3, 2});
    TestTrue(TEXT("Fixed count preserves trailing empty positions"), RPGActionBarLayout::Resolve(OldProfile, 4, Order)
        && Order == TArray<int32>{0, 1, 2, INDEX_NONE});
    const TArray<int32> NoActions;
    TestTrue(TEXT("Fully empty fixed row is supported"), RPGActionBarLayout::Resolve(NoActions, 4, Order)
        && Order == TArray<int32>{INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarSparseRow, "ProjectRPG.UI.ActionBar.Layout.SparseRowAndInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarSparseRow::RunTest(const FString&)
{
    using namespace RPGActionBarLayoutTests;
    TArray<int32> Order;
    const TArray<int32> Positions{3};
    if (!TestTrue(TEXT("Single fourth-position action"), RPGActionBarLayout::Resolve(Positions, 4, Order))) return false;
    int32 BuiltCells = 0, Presses = 0, PressedAction = INDEX_NONE;
    const auto Row = RPGActionBarLayout::CreateRow(Order, [&](int32 ActionIndex) -> TSharedRef<SWidget> {
        ++BuiltCells;
        FRPGActionBarActionView View; View.bAvailable = true;
        return SNew(SRPGActionBarCell).View(View).OnPressed_Lambda([&, ActionIndex] { ++Presses; PressedAction = ActionIndex; });
    });
    TestEqual(TEXT("Only the actual action creates a cell"), BuiltCells, 1);
    if (!TestEqual(TEXT("Four visible positions"), Row->GetChildren()->Num(), 4)) return false;
    for (int32 I = 0; I < 3; ++I)
    {
        const auto Gap = Row->GetChildren()->GetChildAt(I);
        TestFalse(TEXT("Empty position has no action button"), FindButton(Gap).IsValid());
        TestEqual(TEXT("Empty position shares the slot frame"), Gap->GetType(), FName(TEXT("SRPGSlotFrame")));
        TestTrue(TEXT("Empty position can receive hover"), Gap->GetVisibility().IsHitTestVisible());
        TestFalse(TEXT("Empty position cannot handle action input"),
            Gap->OnMouseButtonDown(FGeometry(), FPointerEvent()).IsEventHandled());
    }
    Row->SlatePrepass(1.f);
    FArrangedChildren Arranged(EVisibility::Visible);
    Row->ArrangeChildren(FGeometry::MakeRoot(FVector2f(400.f, 88.f), FSlateLayoutTransform()), Arranged, true);
    if (!TestEqual(TEXT("Gaps participate in native layout"), Arranged.Num(), 4)) return false;
    TestTrue(TEXT("Fourth cell follows all three gaps"), Arranged[3].Geometry.GetAbsolutePosition().X
        >= Arranged[2].Geometry.GetAbsolutePosition().X + Arranged[2].Geometry.GetLocalSize().X);
    auto Button = StaticCastSharedPtr<SButton>(FindButton(Row->GetChildren()->GetChildAt(3)));
    if (!TestTrue(TEXT("Fourth position contains the real native button"), Button.IsValid())) return false;
    const FPointerEvent Mouse(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>{EKeys::LeftMouseButton},
        EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    Button->OnMouseButtonDown(FGeometry(), Mouse);
    Button->OnMouseButtonUp(FGeometry(), Mouse);
    TestEqual(TEXT("Pointer dispatches once"), Presses, 1);
    TestEqual(TEXT("Visual position does not replace action identity"), PressedAction, 0);
    return true;
}
#endif
