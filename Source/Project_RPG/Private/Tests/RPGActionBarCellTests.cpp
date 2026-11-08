#if WITH_DEV_AUTOMATION_TESTS
#include "UI/ActionBar/SRPGActionBarCell.h"
#include "Misc/AutomationTest.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "InputCoreTypes.h"
#include "Widgets/Images/SImage.h"
#include "Engine/Texture2D.h"
#include "UObject/GarbageCollection.h"
#include "Widgets/Notifications/SProgressBar.h"
#include <limits>

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
    // The key/cooldown text precedes the progress row in the actual button content.
    auto Button = FindWidget(Cell, TEXT("SButton"));
    auto Row = Button ? FindWidget(Button.ToSharedRef(), TEXT("SVerticalBox")) : nullptr;
    if (!Row || Row->GetChildren()->Num() < 3) return {};
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
    TestTrue(TEXT("Positive cooldown rounds up while preserving the key"), KeyText(Cell).Contains(TEXT("Q"))
        && KeyText(Cell).Contains(FText::AsNumber(3.0).ToString()));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarIconLifetimeTest, "ProjectRPG.UI.ActionBar.Cell.IconLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarIconLifetimeTest::RunTest(const FString&)
{
    using namespace RPGActionBarCellTests;
    TSharedPtr<SImage> RetainedImage;
    TWeakObjectPtr<UTexture2D> Texture;
    {
        FSlateBrush AuthoringBrush;
        Texture = NewObject<UTexture2D>();
        AuthoringBrush.SetResourceObject(Texture.Get());
        AuthoringBrush.ImageSize = FVector2D(36.f, 36.f);
        auto Cell = SNew(SRPGActionBarCell).Icon(&AuthoringBrush);
        RetainedImage = StaticCastSharedPtr<SImage>(FindWidget(Cell, TEXT("SImage")));
        if (!TestTrue(TEXT("Actual icon image"), RetainedImage.IsValid())) return false;
        AuthoringBrush.SetResourceObject(nullptr);
        AuthoringBrush.ImageSize = FVector2D(1.f, 1.f);
        RetainedImage->Invalidate(EInvalidateWidgetReason::Layout);
        RetainedImage->SlatePrepass(1.f);
        if (!TestTrue(TEXT("Caller mutation cannot change displayed size"), RetainedImage->GetDesiredSize().Equals(FVector2D(36.f, 36.f)))) return false;
    }
    // Only the old image remains: its original stack brush and parent cell are gone.
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("Retained image protects the texture from GC"), Texture.IsValid());
    RetainedImage->Invalidate(EInvalidateWidgetReason::Layout);
    RetainedImage->SlatePrepass(1.f);
    TestTrue(TEXT("Retained image can still measure its original icon"), RetainedImage->GetDesiredSize().Equals(FVector2D(36.f, 36.f)));
    RetainedImage.Reset(); // The engine releases the brush/resource after rendering has finished.
    auto Empty = SNew(SRPGActionBarCell);
    auto EmptyImage = StaticCastSharedPtr<SImage>(FindWidget(Empty, TEXT("SImage")));
    if (!TestTrue(TEXT("Absent icon still has an image slot"), EmptyImage.IsValid())) return false;
    EmptyImage->SlatePrepass(1.f);
    TestTrue(TEXT("An absent icon takes no image space"), EmptyImage->GetDesiredSize().IsNearlyZero());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionBarCooldownPresentationTest, "ProjectRPG.UI.ActionBar.Cell.CooldownPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionBarCooldownPresentationTest::RunTest(const FString&)
{
    using namespace RPGActionBarCellTests;
    FRPGActionBarActionView View;
    View.KeyLabel = FText::FromString(TEXT("R"));
    View.CooldownRemaining = 2.2f; View.CooldownDuration = 10.f;
    TSharedPtr<SRPGActionBarCell> Cell = SNew(SRPGActionBarCell).View_Lambda([&] { return View; });
    const auto Progress = FindWidget(Cell.ToSharedRef(), TEXT("SProgressBar"));
    if (!TestTrue(TEXT("Actual native cooldown gauge"), Progress.IsValid())) return false;
    auto IsGaugeVisible = [&] { Progress->UpdateAllAttributes(); return Progress->GetVisibility().IsVisible(); };
    TestTrue(TEXT("Known cooldown is visible even while unavailable"), IsGaugeVisible());
    TestTrue(TEXT("Current key and rounded time share the same cell"), KeyText(Cell.ToSharedRef()).Contains(TEXT("R"))
        && KeyText(Cell.ToSharedRef()).Contains(FText::AsNumber(3.0).ToString()));
    View.KeyLabel = FText::FromString(TEXT("T")); View.CooldownRemaining = 1.1f;
    TestTrue(TEXT("Existing text follows remapping and provider updates"), KeyText(Cell.ToSharedRef()).Contains(TEXT("T"))
        && KeyText(Cell.ToSharedRef()).Contains(FText::AsNumber(2.0).ToString()));
    View.CooldownDuration = 0.f;
    TestFalse(TEXT("Unknown total duration has no invented fraction"), IsGaugeVisible());
    TestTrue(TEXT("Unknown total preserves reported time and key"), KeyText(Cell.ToSharedRef()).Contains(TEXT("T"))
        && KeyText(Cell.ToSharedRef()).Contains(FText::AsNumber(2.0).ToString()));
    View.CooldownDuration = std::numeric_limits<float>::infinity();
    TestFalse(TEXT("Infinite duration is hidden"), IsGaugeVisible());
    View.CooldownDuration = -1.f;
    TestFalse(TEXT("Negative duration is hidden"), IsGaugeVisible());
    View.CooldownDuration = 10.f; View.CooldownRemaining = 0.f;
    TestFalse(TEXT("Completed cooldown hides the gauge"), IsGaugeVisible());
    TestEqual(TEXT("Completed cooldown keeps just the current key"), KeyText(Cell.ToSharedRef()), FString(TEXT("T")));
    View.CooldownRemaining = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Invalid remaining time is hidden"), IsGaugeVisible());
    TestEqual(TEXT("Invalid remaining time cannot enter displayed text"), KeyText(Cell.ToSharedRef()), FString(TEXT("T")));
    View.CooldownRemaining = 30.f;
    TestTrue(TEXT("Overrun remains a known cooldown"), IsGaugeVisible());
    Cell.Reset();
    TestFalse(TEXT("Retained gauge is hidden after parent cell removal"), IsGaugeVisible());
    return true;
}
#endif
