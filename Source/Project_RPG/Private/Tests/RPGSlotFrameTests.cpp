#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Slots/SRPGSlotFrame.h"
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SVirtualWindow.h"

namespace RPGSlotFrameTests
{
FGeometry Geometry() { return FGeometry::MakeRoot(FVector2f(64.f, 64.f), FSlateLayoutTransform()); }
FPointerEvent Pointer()
{
    return FPointerEvent(0, FVector2D(32.f), FVector2D(32.f), TSet<FKey>{},
        EKeys::Invalid, 0.f, FModifierKeysState());
}
int32 Paint(const TSharedRef<SRPGSlotFrame>& Frame, bool bParentEnabled = true)
{
    const auto Host = SNew(SVirtualWindow).Size(FVector2D(64.f));
    FHittestGrid Grid;
    Grid.SetHittestArea(FVector2D::ZeroVector, FVector2D(64.f));
    FSlateWindowElementList Elements(Host);
    const FPaintArgs Args(&Host.Get(), Grid, FVector2D::ZeroVector, 0.0, 0.f);
    Frame->SlatePrepass(1.f);
    return Frame->Paint(Args, Geometry(), Geometry().GetLayoutBoundingRect(), Elements, 0,
        FWidgetStyle(), bParentEnabled);
}
TSharedPtr<SWidget> Find(const TSharedRef<SWidget>& Root, FName Type)
{
    Root->UpdateAllAttributes();
    if (Root->GetType() == Type) return Root;
    auto* Children = Root->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Found = Find(Children->GetChildAt(I), Type)) return Found;
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlotFrameHoverLifecycle, "ProjectRPG.UI.SlotFrame.HoverLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSlotFrameHoverLifecycle::RunTest(const FString&)
{
    using namespace RPGSlotFrameTests;
    const auto Frame = SNew(SRPGSlotFrame);
    TestEqual(TEXT("Original appearance without hover"), Paint(Frame), 0);
    Frame->OnMouseEnter(Geometry(), Pointer());
    Frame->Tick(Geometry(), 0.0, 0.06f);
    TestEqual(TEXT("Hover adds the shared border layer"), Paint(Frame), 1);
    Frame->OnMouseLeave(Pointer());
    TestEqual(TEXT("Leave immediately restores original appearance without another tick"), Paint(Frame), 0);
    Frame->OnMouseEnter(Geometry(), Pointer());
    TestEqual(TEXT("Reentry starts from zero instead of a stale fade"), Paint(Frame), 0);
    Frame->Tick(Geometry(), 0.1, 0.12f);
    TestEqual(TEXT("Reentry can highlight again"), Paint(Frame), 1);
    Frame->ResetHover();
    TestEqual(TEXT("Owner deactivation resets cached presentation"), Paint(Frame), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlotFrameAppearance, "ProjectRPG.UI.SlotFrame.AppearanceAndDisabled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSlotFrameAppearance::RunTest(const FString&)
{
    using namespace RPGSlotFrameTests;
    FRPGSlotAppearance Appearance;
    const auto Frame = SNew(SRPGSlotFrame).Appearance_Lambda([&] { return Appearance; });
    Frame->OnMouseEnter(Geometry(), Pointer());
    Frame->Tick(Geometry(), 0.0, 0.12f);
    TestEqual(TEXT("Default common appearance"), Paint(Frame), 1);
    Appearance.HighlightWidth = 0.f;
    TestEqual(TEXT("Existing owner appearance changes are read live"), Paint(Frame), 0);
    Appearance.HighlightWidth = 6.f;
    TestEqual(TEXT("Restored width uses the same frame"), Paint(Frame), 1);
    TestEqual(TEXT("Disabled ancestor does not glow"), Paint(Frame, false), 0);
    Frame->SetEnabled(false);
    Frame->Tick(Geometry(), 0.2, 0.01f);
    TestEqual(TEXT("Disabled frame restores the original"), Paint(Frame), 0);
    Frame->SetEnabled(true);
    TestEqual(TEXT("Reenabling has no stale intensity"), Paint(Frame), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlotFrameInput, "ProjectRPG.UI.SlotFrame.ContentAndInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSlotFrameInput::RunTest(const FString&)
{
    using namespace RPGSlotFrameTests;
    int32 Presses = 0, Releases = 0;
    const auto Button = SNew(SButton).IsFocusable(false)
        .OnPressed_Lambda([&] { ++Presses; }).OnReleased_Lambda([&] { ++Releases; });
    const auto Content = SNew(SBox).WidthOverride(48.f).HeightOverride(52.f)[Button];
    const auto Frame = SNew(SRPGSlotFrame)[Content];
    Frame->SlatePrepass(1.f);
    TestTrue(TEXT("Frame preserves the caller's desired size"), Frame->GetDesiredSize().Equals(Content->GetDesiredSize()));
    TestFalse(TEXT("Frame never takes keyboard focus"), Frame->SupportsKeyboardFocus());
    const FPointerEvent Mouse(0, FVector2D(16.f), FVector2D(16.f), TSet<FKey>{EKeys::LeftMouseButton},
        EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    TestFalse(TEXT("Frame never consumes the owner's press"), Frame->OnMouseButtonDown(Geometry(), Mouse).IsEventHandled());
    TestFalse(TEXT("Frame never consumes the owner's release"), Frame->OnMouseButtonUp(Geometry(), Mouse).IsEventHandled());
    Button->OnMouseButtonDown(Geometry(), Mouse);
    Button->OnMouseButtonUp(Geometry(), Mouse);
    TestEqual(TEXT("Existing child dispatches one press"), Presses, 1);
    TestEqual(TEXT("Existing child dispatches one release"), Releases, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlotFrameNativeAdapter, "ProjectRPG.UI.SlotFrame.NativeAdapter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSlotFrameNativeAdapter::RunTest(const FString&)
{
    using namespace RPGSlotFrameTests;
    const auto Cell = SNew(SRPGActionBarCell);
    const auto Found = Find(Cell, TEXT("SRPGSlotFrame"));
    if (!TestTrue(TEXT("Native action uses the shared frame"), Found.IsValid())) return false;
    const auto Frame = StaticCastSharedPtr<SRPGSlotFrame>(Found);
    Frame->OnMouseEnter(Geometry(), Pointer());
    Frame->Tick(Geometry(), 0.0, 0.12f);
    const auto Button = Find(Cell, TEXT("SButton"));
    TestTrue(TEXT("Unavailable action retains an enabled hover host"), Frame->IsEnabled());
    TestFalse(TEXT("Decoration cannot enable an unavailable action"), Button->IsEnabled());
    Frame->OnMouseLeave(Pointer());
    TestTrue(TEXT("Action remains unavailable after hover"), !Button->IsEnabled());
    return true;
}
#endif
