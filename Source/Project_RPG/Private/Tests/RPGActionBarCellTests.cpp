#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Misc/AutomationTest.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "InputCoreTypes.h"

namespace RPGActionBarCellTests
{
TSharedPtr<SWidget> FindWidget(const TSharedRef<SWidget>& Root, FName Type)
{
    Root->UpdateAllAttributes();
    if (Root->GetType() == Type) return Root;
    FChildren* Children = Root->GetChildren();
    for (int32 I = 0; I < Children->Num(); ++I)
        if (auto Found = FindWidget(Children->GetChildAt(I), Type)) return Found;
    return nullptr;
}
FString ToolTipText(const TSharedRef<SRPGActionBarCell>& Cell)
{
    auto Tip = Cell->GetToolTip();
    if (!Tip) return {};
    auto Text = FindWidget(Tip->GetContentWidget(), TEXT("STextBlock"));
    return Text ? StaticCastSharedPtr<STextBlock>(Text)->GetText().ToString() : FString();
}
FString KeyText(const TSharedRef<SRPGActionBarCell>& Cell)
{
    // The key/cooldown block is the last child in the actual native button content.
    auto Button = FindWidget(Cell, TEXT("SButton"));
    auto Row = Button ? FindWidget(Button.ToSharedRef(), TEXT("SVerticalBox")) : nullptr;
    if (!Row || Row->GetChildren()->Num() != 3) return {};
    return StaticCastSharedRef<STextBlock>(Row->GetChildren()->GetChildAt(2))->GetText().ToString();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarDisabledTooltipTest, "ProjectRPG.UI.ActionBar.Cell.DisabledTooltip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarDisabledTooltipTest::RunTest(const FString&)
{
    using namespace RPGActionBarCellTests;
    FRPGActionBarActionView View;
    View.UnavailableReason = FText::FromString(TEXT("Blocked by region"));
    int32 Presses = 0;
    auto Cell = SNew(SRPGActionBarCell).Label(FText::FromString(TEXT("Dash")))
        .View_Lambda([&] { return View; }).OnPressed_Lambda([&] { ++Presses; });
    auto Button = StaticCastSharedPtr<SButton>(FindWidget(Cell, TEXT("SButton")));
    if (!TestTrue(TEXT("Native button exists"), Button.IsValid())) return false;
    TestTrue(TEXT("Tooltip host stays enabled"), Cell->IsEnabled());
    TestFalse(TEXT("Unavailable action is disabled"), Button->IsEnabled());
    TestTrue(TEXT("Tooltip exists on enabled host"), Cell->GetToolTip() && !Cell->GetToolTip()->IsEmpty());
    TestEqual(TEXT("Label and current reason"), ToolTipText(Cell), FString(TEXT("Dash\nBlocked by region")));
    const FPointerEvent Mouse(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>{EKeys::LeftMouseButton},
        EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    TestFalse(TEXT("Disabled pointer press is rejected"), Button->OnMouseButtonDown(FGeometry(), Mouse).IsEventHandled());
    TestEqual(TEXT("Disabled action never dispatches"), Presses, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarLiveFeedbackTest, "ProjectRPG.UI.ActionBar.Cell.LiveFeedback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarLiveFeedbackTest::RunTest(const FString&)
{
    using namespace RPGActionBarCellTests;
    FRPGActionBarActionView View;
    View.UnavailableReason = FText::FromString(TEXT("Waiting for server"));
    auto Cell = SNew(SRPGActionBarCell).Label(FText::FromString(TEXT("Dash"))).View_Lambda([&] { return View; });
    auto Tip = Cell->GetToolTip();
    TestEqual(TEXT("First reason"), ToolTipText(Cell), FString(TEXT("Dash\nWaiting for server")));
    View.UnavailableReason = FText::FromString(TEXT("In combat"));
    TestEqual(TEXT("Reason changes on the existing tooltip"), ToolTipText(Cell), FString(TEXT("Dash\nIn combat")));
    TestTrue(TEXT("No tooltip rebuild needed"), Cell->GetToolTip() == Tip);
    View.bAvailable = true;
    TestEqual(TEXT("Available action hides stale reason"), ToolTipText(Cell), FString(TEXT("Dash")));
    View.bAvailable = false;
    View.UnavailableReason = FText::GetEmpty();
    const FString Fallback = ToolTipText(Cell);
    TestTrue(TEXT("Empty reason has a localized fallback"), Fallback.StartsWith(TEXT("Dash\n")) && Fallback.Len() > 5);
    auto Empty = SNew(SRPGActionBarCell);
    TestFalse(TEXT("Unbound view remains unavailable"), FindWidget(Empty, TEXT("SButton"))->IsEnabled());
    TestFalse(TEXT("Unbound view still has feedback"), ToolTipText(Empty).IsEmpty());
    TSharedPtr<SRPGActionBarCell> Retiring;
    SAssignNew(Retiring, SRPGActionBarCell).Label(FText::FromString(TEXT("Retiring")))
        .View_Lambda([&] { return View; });
    auto RetainedTip = Retiring->GetToolTip();
    auto RetainedButton = FindWidget(Retiring.ToSharedRef(), TEXT("SButton"));
    Retiring.Reset();
    RetainedButton->UpdateAllAttributes();
    TestTrue(TEXT("Tooltip held by Slate becomes empty after cell removal"), RetainedTip->IsEmpty());
    TestFalse(TEXT("Retained button cannot execute after cell removal"), RetainedButton->IsEnabled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarCooldownReleaseTest, "ProjectRPG.UI.ActionBar.Cell.CooldownAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarCooldownReleaseTest::RunTest(const FString&)
{
    using namespace RPGActionBarCellTests;
    FRPGActionBarActionView View;
    View.bAvailable = true;
    View.KeyLabel = FText::FromString(TEXT("Q"));
    bool bHeld = false;
    int32 Presses = 0, Releases = 0;
    auto Cell = SNew(SRPGActionBarCell).View_Lambda([&] { return View; }).IsHeld_Lambda([&] { return bHeld; })
        .OnPressed_Lambda([&] { bHeld = true; ++Presses; })
        .OnReleased_Lambda([&] { bHeld = false; ++Releases; });
    auto Button = StaticCastSharedPtr<SButton>(FindWidget(Cell, TEXT("SButton")));
    TestEqual(TEXT("Current binding is visible"), KeyText(Cell), FString(TEXT("Q")));
    View.CooldownRemaining = 2.2f;
    TestEqual(TEXT("Positive cooldown rounds up"), KeyText(Cell), FText::AsNumber(3.0).ToString());
    View.CooldownRemaining = 0.f; View.KeyLabel = FText::FromString(TEXT("E"));
    TestEqual(TEXT("Binding changes without rebuilding"), KeyText(Cell), FString(TEXT("E")));
    View.CooldownRemaining = TNumericLimits<float>::Max();
    TestFalse(TEXT("Large finite cooldown does not wrap to negative"), KeyText(Cell).Contains(TEXT("-")));
    const FPointerEvent Mouse(0, FVector2D::ZeroVector, FVector2D::ZeroVector, TSet<FKey>{EKeys::LeftMouseButton},
        EKeys::LeftMouseButton, 0.f, FModifierKeysState());
    Button->OnMouseButtonDown(FGeometry(), Mouse);
    TestEqual(TEXT("Pointer press reaches caller once"), Presses, 1);
    View.bAvailable = false;
    Button->UpdateAllAttributes();
    TestTrue(TEXT("Held pointer can still release after availability changes"), Button->IsEnabled());
    Button->OnMouseButtonUp(FGeometry(), Mouse);
    Button->UpdateAllAttributes();
    TestEqual(TEXT("Release reaches caller once"), Releases, 1);
    TestFalse(TEXT("Released unavailable button is disabled"), Button->IsEnabled());
    return true;
}
#endif
