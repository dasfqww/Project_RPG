#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/RPGWindowInputState.h"
#include "Widgets/SWidget.h"

/** Tests only local window routing. No login, item fixtures, RPCs or persistence. */
struct FRPGItemWindowTestAccess
{
	UWorld* World = nullptr;
	ARPGPlayerController* Controller = nullptr;
	TArray<TSharedPtr<SWidget>> SlateWindows;

	FRPGItemWindowTestAccess()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<ARPGPlayerController>();
		if (!Controller) return;
		Controller->SetAsLocalPlayerController();
		// Native mail provides a complete local widget tree without a game viewport.
		Controller->InventoryProjectionWidget = MakeWindow();
		Controller->EquipmentProjectionWidget = MakeWindow();
		Controller->RewardMailWidget = MakeWindow();
	}
	~FRPGItemWindowTestAccess()
	{
		if (Controller) Controller->WindowInputState->Reset();
		SlateWindows.Reset();
		if (World) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
	}
	URPGRewardMailWidget* MakeWindow()
	{
		auto* Window = NewObject<URPGRewardMailWidget>(Controller);
		Window->Initialize();
		SlateWindows.Add(Window->TakeWidget());
		Register(*Window);
		return Window;
	}
	void Register(URPGInventoryProjectionWidget& Window)
	{
		Window.OnWindowClosed.AddUObject(Controller, &ARPGPlayerController::HandleProjectionWindowClosed, &Window);
		Controller->WindowInputState->BeginWindow(*Controller, Window);
		Window.SetVisibility(ESlateVisibility::Visible);
		Window.ActivateWidget();
	}
	URPGInventoryProjectionWidget* Bag() const { return Controller->InventoryProjectionWidget; }
	URPGInventoryProjectionWidget* Equipment() const { return Controller->EquipmentProjectionWidget; }
	URPGRewardMailWidget* Mail() const { return Controller->RewardMailWidget; }
	bool IsOpen(UUserWidget* Window) const { return Controller->WindowInputState->ContainsWindow(*Window); }
	void Focus(UUserWidget& Window) const { Controller->WindowInputState->FocusWindow(Window); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIndependentItemWindowRoutingTest,
	"ProjectRPG.Item.UI.IndependentWindows.BagToggleAndTopClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGIndependentItemWindowRoutingTest::RunTest(const FString&)
{
	FRPGItemWindowTestAccess Fixture;
	if (!TestNotNull(TEXT("Independent window controller"), Fixture.Controller)) return false;
	auto* Shared = Fixture.Controller->GetInventoryProjectionViewModel();
	const auto EquipmentKey = [](bool bRepeat) { return FKeyEvent(EKeys::P, FModifierKeysState(), 0, bRepeat, 0, 0); };
	TestTrue(TEXT("P repeats are consumed by the focused item window"),
		Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Mail(), EquipmentKey(true)));
	TestTrue(TEXT("P repeats do not close equipment"), Fixture.IsOpen(Fixture.Equipment()));
	TestTrue(TEXT("P can route from mail to toggle equipment"),
		Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Mail(), EquipmentKey(false)));
	TestFalse(TEXT("P closes only equipment"), Fixture.IsOpen(Fixture.Equipment()));
	TestTrue(TEXT("Bag and mail survive the equipment toggle"), Fixture.IsOpen(Fixture.Bag()) && Fixture.IsOpen(Fixture.Mail()));
	Fixture.Register(*Fixture.Equipment());
	Fixture.Focus(*Fixture.Mail());
	Fixture.Controller->ToggleInventory();
	TestFalse(TEXT("Inventory toggle closes only the bag"), Fixture.IsOpen(Fixture.Bag()));
	TestTrue(TEXT("Equipment and mail stay open"), Fixture.IsOpen(Fixture.Equipment()) && Fixture.IsOpen(Fixture.Mail()));
	const auto Escape = [](bool bRepeat) { return FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, bRepeat, 0, 0); };
	TestTrue(TEXT("Repeat Escape is consumed"), Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Equipment(), Escape(true)));
	TestTrue(TEXT("Repeat Escape does not close mail"), Fixture.IsOpen(Fixture.Mail()));
	TestTrue(TEXT("Escape can route from another open window"),
		Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Equipment(), Escape(false)));
	TestFalse(TEXT("Escape closes the top mail window, not its event source"), Fixture.IsOpen(Fixture.Mail()));
	TestTrue(TEXT("Equipment still retains the input session"), Fixture.IsOpen(Fixture.Equipment()) && Fixture.Controller->bShowMouseCursor);
	Fixture.Register(*Fixture.Bag());
	auto* Close = Cast<UButton>(Fixture.Equipment()->GetWidgetFromName(TEXT("CloseWindowButton")));
	if (!TestNotNull(TEXT("Individual equipment close control"), Close)) return false;
	Close->OnClicked.Broadcast();
	TestFalse(TEXT("Close control removes only equipment"), Fixture.IsOpen(Fixture.Equipment()));
	TestTrue(TEXT("Bag remains open"), Fixture.IsOpen(Fixture.Bag()));
	TestTrue(TEXT("Closing windows retains the controller-owned model"), Shared == Fixture.Controller->GetInventoryProjectionViewModel());
	TestFalse(TEXT("Closed windows cannot route new UI hotkeys"),
		Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Mail(), Escape(false)));
	Fixture.Controller->HandlePlayerWindowKeyDown(Fixture.Bag(), Escape(false));
	TestFalse(TEXT("Last close hides the cursor"), Fixture.Controller->bShowMouseCursor);
	TestFalse(TEXT("Last close releases the window movement lock"), Fixture.Controller->IsMoveInputIgnored());
	TestFalse(TEXT("Last close releases the window camera lock"), Fixture.Controller->IsLookInputIgnored());
	return true;
}

#endif
