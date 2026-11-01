#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/GameViewportSubsystem.h"
#include "Misc/AutomationTest.h"
#include "UI/RPGWindowPlacement.h"
#include "UI/Skill/RPGSkillWindow.h"
#include "Widgets/SWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWindowPlacementGeometryTest,
	"ProjectRPG.Skill.UI.WindowPlacement.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGWindowPlacementGeometryTest::RunTest(const FString&)
{
	const auto Origin = [](const FRPGWindowPlacement& Placement, const FVector2D& Screen, const FVector2D& Size)
	{
		return Placement.Anchor * Screen + Placement.Offset - Placement.Alignment * Size;
	};
	const FVector2D SkillSize(960.0, 640.0);
	const FVector2D InventorySize(720.0, 640.0);
	for (const double Scale : {0.5, 2.0 / 3.0, 1.0, 1.5, 2.0})
	{
		const FVector2D LogicalViewport = (FVector2D(1920.0, 1080.0) * Scale) / Scale;
		const auto Left = RPGWindowPlacement::Calculate(LogicalViewport, SkillSize, ERPGWindowSide::Left);
		const auto Right = RPGWindowPlacement::Calculate(LogicalViewport, InventorySize, ERPGWindowSide::Right);
		const FVector2D L = Origin(Left, LogicalViewport, SkillSize);
		const FVector2D R = Origin(Right, LogicalViewport, InventorySize);
		TestTrue(TEXT("Windows with enough space including margins do not overlap"), L.X + SkillSize.X <= R.X);
		TestTrue(TEXT("Skill window retains the left inset"), FMath::IsNearlyEqual(L.X, 24.0));
		TestTrue(TEXT("Inventory retains the right inset"), FMath::IsNearlyEqual(R.X + InventorySize.X, LogicalViewport.X - 24.0));
		TestTrue(TEXT("Fitting windows remain vertically centered"), FMath::IsNearlyEqual(L.Y, 220.0));
	}

	const FVector2D SmallScreen(800.0, 600.0);
	const auto Mail = RPGWindowPlacement::Calculate(SmallScreen, FVector2D(720.0, 540.0), ERPGWindowSide::Center);
	TestTrue(TEXT("Mail has an independent centered baseline"),
		Origin(Mail, SmallScreen, FVector2D(720.0, 540.0)).Equals(FVector2D(40.0, 30.0)));
	for (const ERPGWindowSide Side : {ERPGWindowSide::Left, ERPGWindowSide::Right})
	{
		const auto Oversized = RPGWindowPlacement::Calculate(SmallScreen, SkillSize, Side);
		TestTrue(TEXT("Oversized title is aligned to the top"), FMath::IsNearlyZero(Origin(Oversized, SmallScreen, SkillSize).Y));
		TestTrue(TEXT("Oversized window gives up its horizontal margin"), FMath::IsNearlyZero(Oversized.Offset.X));
		const auto ExactFit = RPGWindowPlacement::Calculate(SmallScreen, SmallScreen, Side);
		TestTrue(TEXT("Exact fit stays within all edges"), Origin(ExactFit, SmallScreen, SmallScreen).IsNearlyZero());
	}
	const auto AlmostFull = RPGWindowPlacement::Calculate(SmallScreen, FVector2D(780.0, 500.0), ERPGWindowSide::Left);
	TestTrue(TEXT("Margin shrinks before clipping a fitting window"), FMath::IsNearlyEqual(AlmostFull.Offset.X, 10.0));
	const auto PendingGeometry = RPGWindowPlacement::Calculate(FVector2D::ZeroVector, SkillSize, ERPGWindowSide::Right);
	TestTrue(TEXT("Missing viewport geometry still uses a responsive right anchor"), PendingGeometry.Anchor.Equals(FVector2D(1.0, 0.5)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWindowPlacementDesiredSlotTest,
	"ProjectRPG.Skill.UI.WindowPlacement.DesiredSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGWindowPlacementDesiredSlotTest::RunTest(const FString&)
{
	UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get();
	if (!TestNotNull(TEXT("Viewport subsystem"), Viewport)) return false;
	UClass* Class = LoadClass<URPGSkillWindow>(nullptr,
		TEXT("/Game/Blueprints/UI/Skill/WBP_SkillWindow.WBP_SkillWindow_C"));
	if (!TestNotNull(TEXT("Authored desired-size skill window"), Class)) return false;
	URPGSkillWindow* Window = NewObject<URPGSkillWindow>(GetTransientPackage(), Class);
	if (!TestTrue(TEXT("Widget initializes"), Window->Initialize())) return false;
	// No viewport hosts this test widget. Keep its Slate wrapper alive just as
	// AddToViewport does in gameplay; dropping it immediately destroys the tree.
	const TSharedRef<SWidget> SlateWindow = Window->TakeWidget();
	Window->ActivateWidget(); // Authored activation graph first applies its centered layout.
	SlateWindow->SlatePrepass(1.f);
	FGameViewportWidgetSlot InitialSlot = Viewport->GetWidgetSlot(Window);
	InitialSlot.ZOrder = 40;
	Viewport->SetWidgetSlot(Window, InitialSlot);
	const FVector2D Translation(15.0, 25.0);
	Window->SetRenderTranslation(Translation);
	for (const ERPGWindowSide Side : {ERPGWindowSide::Left, ERPGWindowSide::Right})
	{
		RPGWindowPlacement::Apply(*Window, Side);
		const FGameViewportWidgetSlot Slot = Viewport->GetWidgetSlot(Window);
		const double X = Side == ERPGWindowSide::Left ? 0.0 : 1.0;
		TestTrue(TEXT("Authored 960x640 Desired size is preserved"), Window->GetDesiredSize().Equals(FVector2D(960.0, 640.0)));
		TestTrue(TEXT("Opening placement wins over authored centering"), Slot.Anchors == FAnchors(X, 0.5));
		TestTrue(TEXT("Alignment matches the non-stretched anchor"), Slot.Alignment.Equals(FVector2D(X, 0.5)));
		TestTrue(TEXT("Zero extent keeps natural Desired size, not fullscreen stretch"), FMath::IsNearlyZero(Slot.Offsets.Right) && FMath::IsNearlyZero(Slot.Offsets.Bottom));
		TestEqual(TEXT("Placement preserves viewport Z-order"), Slot.ZOrder, 40);
		TestTrue(TEXT("Placement preserves user drag translation"), Window->GetRenderTransform().Translation.Equals(Translation));
	}
	Window->DeactivateWidget();
	Window->RemoveFromParent();
	return true;
}

#endif
