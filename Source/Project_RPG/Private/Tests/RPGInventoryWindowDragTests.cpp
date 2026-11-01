#if WITH_DEV_AUTOMATION_TESTS

#include "Input/Events.h"
#include "Components/Button.h"
#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/RPGWindowDragConstraints.h"
#include "UObject/UnrealType.h"
#include "Widgets/SVirtualWindow.h"

namespace RPGInventoryWindowDragTests
{
FPointerEvent Pointer(const FVector2D Position, const FKey Button, const bool bHeld)
{
	TSet<FKey> PressedButtons;
	if (bHeld) PressedButtons.Add(Button);
	return FPointerEvent(0, Position, Position, PressedButtons, Button, 0.f, FModifierKeysState());
}

// Populate the authored title's cached geometry without opening a window,
// starting PIE, creating a player, or touching item/backend state.
FGeometry PaintWindow(const TSharedRef<SWidget>& Widget, const float Scale)
{
	const FVector2D Size(800.f, 900.f);
	const TSharedRef<SVirtualWindow> PaintHost = SNew(SVirtualWindow).Size(Size);
	FHittestGrid Grid;
	Grid.SetHittestArea(FVector2D::ZeroVector, Size * Scale);
	FSlateWindowElementList Elements(PaintHost);
	const FGeometry Geometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform(Scale));
	const FPaintArgs Args(&PaintHost.Get(), Grid, FVector2D::ZeroVector, 0.0, 0.f);
	Widget->SlatePrepass(Scale);
	Widget->Paint(Args, Geometry, Geometry.GetLayoutBoundingRect(), Elements, 0, FWidgetStyle(), true);
	return Geometry;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryWindowLeftDragTest,
	"ProjectRPG.Item.UI.WindowDrag.LeftButtonTitleOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGInventoryWindowLeftDragTest::RunTest(const FString&)
{
	using namespace RPGInventoryWindowDragTests;
	const TCHAR* Classes[] = {
		TEXT("/Game/Blueprints/UI/Inventory/Projection/WBP_InventoryProjection.WBP_InventoryProjection_C"),
		TEXT("/Game/Blueprints/UI/Equipment/Projection/WBP_EquipmentProjection.WBP_EquipmentProjection_C")
	};
	for (const TCHAR* Path : Classes)
	{
		UClass* Class = LoadClass<URPGInventoryProjectionWidget>(nullptr, Path);
		if (!TestNotNull(TEXT("Authored item window"), Class)) return false;
		URPGInventoryProjectionWidget* Window = NewObject<URPGInventoryProjectionWidget>(GetTransientPackage(), Class);
		if (!TestTrue(TEXT("Item window initializes"), Window->Initialize())) return false;
		const TSharedRef<SWidget> SlateWindow = Window->TakeWidget();
		UWidget* Title = Window->GetWidgetFromName(TEXT("TitleText"));
		if (!TestNotNull(TEXT("Full-width title row exists"), Title)) return false;
		UButton* Close = Cast<UButton>(Window->GetWidgetFromName(TEXT("CloseWindowButton")));
		if (!TestNotNull(TEXT("Authored individual close button"), Close)) return false;
		TestTrue(TEXT("Title text and close button share one header"), Title->GetParent() == Close->GetParent());
		TestEqual(TEXT("Title row can receive pointer hits"), Title->GetVisibility(), ESlateVisibility::Visible);

		for (const float Scale : {0.5f, 1.f, 2.f})
		{
			Window->SetRenderTranslation(FVector2D::ZeroVector);
			const FGeometry Geometry = PaintWindow(SlateWindow, Scale);
			const FGeometry TitleGeometry = Title->GetCachedGeometry();
			if (!TestTrue(TEXT("Title geometry is arranged"), TitleGeometry.GetLocalSize().Y > 0.f)) return false;
			const FVector2D Start = TitleGeometry.LocalToAbsolute(TitleGeometry.GetLocalSize() * 0.5f);
			const FVector2D Outside = Geometry.LocalToAbsolute(FVector2D(400.f, 800.f));
			TestFalse(TEXT("Right title click does not drag an item window"),
				SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::RightMouseButton, true)).IsEventHandled());
			TestFalse(TEXT("Left click outside the title does not drag"),
				SlateWindow->OnMouseButtonDown(Geometry, Pointer(Outside, EKeys::LeftMouseButton, true)).IsEventHandled());
			TestFalse(TEXT("Right click outside the title does not drag"),
				SlateWindow->OnMouseButtonDown(Geometry, Pointer(Outside, EKeys::RightMouseButton, true)).IsEventHandled());
			TestTrue(TEXT("Left title press starts dragging"),
				SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::LeftMouseButton, true)).IsEventHandled());
			const FVector2D End = Start + FVector2D(40.f, 20.f);
			TestTrue(TEXT("Held left move is consumed"),
				SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true)).IsEventHandled());
			const FVector2D Expected = FVector2D(40.f, 20.f) / Scale;
			TestTrue(TEXT("Movement accounts for DPI"), Window->GetRenderTransform().Translation.Equals(Expected));
			SlateWindow->OnMouseButtonUp(Geometry, Pointer(End, EKeys::RightMouseButton, false));
			TestTrue(TEXT("Releasing another button does not end left dragging"),
				SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true)).IsEventHandled());
			SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true));
			TestTrue(TEXT("Repeated move does not accumulate drift"), Window->GetRenderTransform().Translation.Equals(Expected));
			TestTrue(TEXT("Left release ends dragging"),
				SlateWindow->OnMouseButtonUp(Geometry, Pointer(End, EKeys::LeftMouseButton, false)).IsEventHandled());
			SlateWindow->OnMouseMove(Geometry, Pointer(End + FVector2D(30.f, 0.f), EKeys::LeftMouseButton, true));
			TestTrue(TEXT("Window stays in place after release"), Window->GetRenderTransform().Translation.Equals(Expected));

			SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::LeftMouseButton, true));
			SlateWindow->OnMouseCaptureLost(FCaptureLostEvent());
			SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true));
			TestTrue(TEXT("Capture loss cancels dragging"), Window->GetRenderTransform().Translation.Equals(Expected));

			SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::LeftMouseButton, true));
			SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, false));
			SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true));
			TestTrue(TEXT("A missed release cannot leave a sticky drag"), Window->GetRenderTransform().Translation.Equals(Expected));

			Window->ActivateWidget();
			SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::LeftMouseButton, true));
			Window->DeactivateWidget();
			Window->ActivateWidget();
			SlateWindow->OnMouseMove(Geometry, Pointer(End, EKeys::LeftMouseButton, true));
			TestTrue(TEXT("Closing and reopening cancels dragging"), Window->GetRenderTransform().Translation.Equals(Expected));
			Window->DeactivateWidget();
		}
		Window->ActivateWidget();
		int32 Closed = 0;
		const FDelegateHandle Handle = Window->OnWindowClosed.AddLambda([&Closed]() { ++Closed; });
		Close->OnClicked.Broadcast();
		TestFalse(TEXT("Authored close button deactivates only its own window"), Window->IsActivated());
		TestEqual(TEXT("Close button hides its own window"), Window->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("Close button sends one close notification"), Closed, 1);
		Window->OnWindowClosed.Remove(Handle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWindowLegacyDragButtonTest,
	"ProjectRPG.Item.UI.WindowDrag.ExistingWindowsKeepLeftButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGWindowLegacyDragButtonTest::RunTest(const FString&)
{
	using namespace RPGInventoryWindowDragTests;
	URPGWidgetBase* Window = NewObject<URPGWidgetBase>();
	Window->Initialize();
	FBoolProperty* CanDrag = FindFProperty<FBoolProperty>(URPGWidgetBase::StaticClass(), TEXT("bCanDrag"));
	if (!TestNotNull(TEXT("Existing opt-in drag setting"), CanDrag)) return false;
	CanDrag->SetPropertyValue_InContainer(Window, true);
	const TSharedRef<SWidget> SlateWindow = Window->TakeWidget();
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(100.f, 100.f), FSlateLayoutTransform(1.f));
	const FVector2D Start(10.f, 10.f);
	TestFalse(TEXT("Default windows do not switch to right dragging"),
		SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::RightMouseButton, true)).IsEventHandled());
	TestTrue(TEXT("Default windows still start on left press"),
		SlateWindow->OnMouseButtonDown(Geometry, Pointer(Start, EKeys::LeftMouseButton, true)).IsEventHandled());
	SlateWindow->OnMouseMove(Geometry, Pointer(Start + FVector2D(15.f, 5.f), EKeys::LeftMouseButton, true));
	TestTrue(TEXT("Existing left drag still moves"), Window->GetRenderTransform().Translation.Equals(FVector2D(15.f, 5.f)));
	SlateWindow->OnMouseButtonUp(Geometry, Pointer(Start, EKeys::LeftMouseButton, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInventoryWindowDragBoundsTest,
	"ProjectRPG.Item.UI.WindowDrag.TitleBarStaysInBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGInventoryWindowDragBoundsTest::RunTest(const FString&)
{
	using RPGWindowDragConstraints::ClampScreenDelta;
	const FSlateRect Screen(300.f, 200.f, 1300.f, 900.f);
	const FSlateRect Title(450.f, 240.f, 850.f, 280.f);
	TestTrue(TEXT("Free movement inside the viewport is unchanged"),
		ClampScreenDelta(Title, Screen, FVector2D(50.f, 30.f)).Equals(FVector2D(50.f, 30.f)));
	TestTrue(TEXT("Top/left title edges cannot leave the player screen"),
		ClampScreenDelta(Title, Screen, FVector2D(-10000.f, -10000.f)).Equals(FVector2D(-150.f, -40.f)));
	TestTrue(TEXT("Bottom/right title edges cannot leave the player screen"),
		ClampScreenDelta(Title, Screen, FVector2D(10000.f, 10000.f)).Equals(FVector2D(450.f, 620.f)));
	for (const float Scale : {0.5f, 1.f, 2.f})
	{
		const FSlateRect ScaledScreen(Screen.Left * Scale, Screen.Top * Scale,
			Screen.Right * Scale, Screen.Bottom * Scale);
		const FSlateRect ScaledTitle(Title.Left * Scale, Title.Top * Scale,
			Title.Right * Scale, Title.Bottom * Scale);
		TestTrue(TEXT("Screen clipping and DPI conversion give the same layout displacement"),
			(ClampScreenDelta(ScaledTitle, ScaledScreen, FVector2D(10000.f, 10000.f)) / Scale)
			.Equals(FVector2D(450.f, 620.f)));
	}
	const FSlateRect SmallScreen(0.f, 0.f, 800.f, 600.f);
	const FSlateRect OversizedTitle(-300.f, -100.f, 1500.f, 700.f);
	TestTrue(TEXT("An oversized title can pan to its top/left edge"),
		ClampScreenDelta(OversizedTitle, SmallScreen, FVector2D(10000.f, 10000.f)).Equals(FVector2D(300.f, 100.f)));
	TestTrue(TEXT("An oversized title can pan to its bottom/right edge"),
		ClampScreenDelta(OversizedTitle, SmallScreen, FVector2D(-10000.f, -10000.f)).Equals(FVector2D(-700.f, -100.f)));
	const FVector2D Delta(70.f, 45.f);
	TestTrue(TEXT("Unavailable viewport geometry does not pin the window"),
		ClampScreenDelta(Title, FSlateRect(0.f, 0.f, 0.f, 0.f), Delta).Equals(Delta));
	TestTrue(TEXT("Unavailable title geometry does not corrupt movement"),
		ClampScreenDelta(FSlateRect(), Screen, Delta).Equals(Delta));
	return true;
}

#endif
