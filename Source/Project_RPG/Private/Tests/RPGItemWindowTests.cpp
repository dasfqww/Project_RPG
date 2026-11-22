#if WITH_DEV_AUTOMATION_TESTS

#include "Actor/Content/RPGContentEntryActor.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/Pawn.h"
#include "Input/Events.h"
#include "Input/RPGPanelShortcutComponent.h"
#include "Input/RPGPanelShortcutConfig.h"
#include "InputActionValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Slate/SceneViewport.h"
#include "UI/Content/RPGContentEntryWidget.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/MessageBox/RPGMessageBox.h"
#include "UI/RPGWindowInputState.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SViewport.h"
#include "Widgets/SVirtualWindow.h"
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
	static URPGContentEntryWidget* Entry(const ARPGPlayerController& PC) { return PC.ContentEntryPopup; }
	static URPGMessageBox* Confirmation(const ARPGPlayerController& PC) { return PC.ContentEntryConfirmation; }
	static UUserWidget* Top(const ARPGPlayerController& PC) { return PC.WindowInputState->GetTopWindow(); }
	static bool IsMailOpen(const ARPGPlayerController& PC)
	{
		return PC.RewardMailWidget && PC.WindowInputState->ContainsWindow(*PC.RewardMailWidget);
	}
	static void Interact(ARPGPlayerController& PC, AActor& Entry)
	{
		PC.FoundInteractable(&Entry);
		PC.Input_PickUp_Items(FInputActionValue(true));
	}
	static URPGRewardMailWidget* OpenInventory(ARPGPlayerController& PC)
	{
		auto* Inventory = CreateWidget<URPGRewardMailWidget>(&PC);
		PC.InventoryProjectionWidget = Inventory;
		PC.DisplayInventoryProjection(Inventory);
		return Inventory;
	}
	static URPGRewardMailWidget* OpenEquipment(ARPGPlayerController& PC)
	{
		auto* Equipment = CreateWidget<URPGRewardMailWidget>(&PC);
		PC.EquipmentProjectionWidget = Equipment;
		PC.DisplayInventoryProjection(Equipment);
		return Equipment;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPanelShortcutConfigTest,
	"ProjectRPG.Item.UI.PanelShortcuts.EditableBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGPanelShortcutConfigTest::RunTest(const FString&)
{
	auto* Config = NewObject<URPGPanelShortcutConfig>();
	const FInputChord AltM(EKeys::M, EModifierKey::Alt);
	const FInputChord CtrlM(EKeys::M, EModifierKey::Control);
	Config->Shortcuts.Add({AltM, ERPGPanelShortcutAction::RewardMail});
	Config->Shortcuts.Add({CtrlM, ERPGPanelShortcutAction::Inventory});

	TMap<FInputChord, ERPGPanelShortcutAction> Map;
	FText Error;
	TestTrue(TEXT("The same key can serve two distinct combinations"), Config->BuildShortcutMap(Map, Error) && Map.Num() == 2);
	TestTrue(TEXT("Alt+M targets reward mail"), Map.Contains(AltM) && Map.FindRef(AltM) == ERPGPanelShortcutAction::RewardMail);
	Config->Shortcuts.RemoveAt(0);
	TestTrue(TEXT("Removing a row removes only its combination"),
		Config->BuildShortcutMap(Map, Error) && !Map.Contains(AltM) && Map.Contains(CtrlM));

	Config->Shortcuts.Add({CtrlM, ERPGPanelShortcutAction::Equipment});
	TestFalse(TEXT("The exact same combination cannot be assigned twice"), Config->BuildShortcutMap(Map, Error));
	TestTrue(TEXT("An invalid list leaves no partially registered bindings"), Map.IsEmpty() && !Error.IsEmpty());
	Config->Shortcuts = {{FInputChord(EKeys::M), ERPGPanelShortcutAction::RewardMail}};
	TestFalse(TEXT("A bare key is not a panel combination"), Config->BuildShortcutMap(Map, Error));
	Config->Shortcuts = {{FInputChord(EKeys::Enter, EModifierKey::Alt), ERPGPanelShortcutAction::RewardMail}};
	TestFalse(TEXT("The fullscreen combination is reserved"), Config->BuildShortcutMap(Map, Error));
	Config->Shortcuts.Reset();
	TestTrue(TEXT("An empty list disables all panel combinations"), Config->BuildShortcutMap(Map, Error) && Map.IsEmpty());
	return true;
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContentEntryFocusTest,
	"ProjectRPG.Item.UI.IndependentWindows.ContentEntryFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGContentEntryFocusTest::RunTest(const FString&)
{
	using FAccess = FRPGItemWindowTestAccess;
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
	Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/ContentEntryFocus_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
	UWorld* World = Instance->GetWorld();
	auto* Viewport = NewObject<UGameViewportClient>(GEngine);
	Instance->GetWorldContext()->GameViewport = Viewport;
	Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
	const auto Overlay = SNew(SOverlay);
	const auto GameWidget = SNew(SViewport)[Overlay];
	const auto Window = SNew(SVirtualWindow).Size(FVector2D(1280.0, 720.0));
	Window->SetContent(GameWidget);
	auto& Slate = FSlateApplication::Get();
	Slate.RegisterVirtualWindow(Window);
	const auto SceneViewport = Viewport->CreateViewport(GameWidget);
	GameWidget->SetViewportInterface(SceneViewport);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Viewport->SetViewportOverlayWidget(Window, Overlay);
	Viewport->SetViewport(&SceneViewport.Get());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	ARPGPlayerController* PC = nullptr;
	ON_SCOPE_EXIT
	{
		if (PC)
		{
			if (auto* Shortcuts = PC->FindComponentByClass<URPGPanelShortcutComponent>()) Shortcuts->DestroyComponent();
			if (auto* Entry = FAccess::Entry(*PC)) Entry->RemoveFromParent();
			if (auto* Top = FAccess::Top(*PC)) Top->RemoveFromParent();
		}
		Slate.UnregisterVirtualWindow(Window);
		Instance->Shutdown();
		Viewport->DetachViewportClient();
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	FString PlayerError;
	auto* Player = Instance->CreateLocalPlayer(0, PlayerError, false);
	PC = World->SpawnActor<ARPGPlayerController>();
	if (!TestNotNull(TEXT("Local player"), Player) || !TestNotNull(TEXT("Player controller"), PC)) return false;
	PC->SetPlayer(Player);
	World->AddController(PC);
	const auto User = Player->GetSlateUser();
	if (!TestTrue(TEXT("Local player has a Slate user"), User.IsValid())) return false;
	const auto Escape = [&](bool bRepeat = false)
	{
		return Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), User->GetUserIndex(), bRepeat, 0, 0));
	};
	const auto HasFocus = [&](const UUserWidget& Widget)
	{
		return Slate.GetUserFocusedWidget(User->GetUserIndex()) == Widget.GetCachedWidget();
	};
	const auto MailEvent = [&](bool bAlt, bool bCtrl = false, bool bRepeat = false)
	{
		const FModifierKeysState Modifiers(false, false, bCtrl, false, bAlt, false, false, false, false);
		return FKeyEvent(EKeys::M, Modifiers, User->GetUserIndex(), bRepeat, 0, 0);
	};
	Slate.SetUserFocus(User->GetUserIndex(), GameWidget, EFocusCause::SetDirectly);
	TestTrue(TEXT("The game viewport has keyboard focus before opening mail"),
		Slate.GetUserFocusedWidget(User->GetUserIndex()) == GameWidget);
	Slate.ProcessKeyDownEvent(MailEvent(false));
	Slate.ProcessKeyUpEvent(MailEvent(false));
	TestFalse(TEXT("Plain M does not open mail"), FAccess::IsMailOpen(*PC));
	Slate.ProcessKeyDownEvent(MailEvent(true, true));
	Slate.ProcessKeyUpEvent(MailEvent(true, true));
	TestFalse(TEXT("An extra modifier does not match Alt+M"), FAccess::IsMailOpen(*PC));
	TestTrue(TEXT("Alt+M opens mail from the game viewport"),
		Slate.ProcessKeyDownEvent(MailEvent(true)) && FAccess::IsMailOpen(*PC));
	TestTrue(TEXT("Holding Alt+M consumes repeats without closing mail"),
		Slate.ProcessKeyDownEvent(MailEvent(true, false, true)) && FAccess::IsMailOpen(*PC));
	TestTrue(TEXT("Releasing Alt first still consumes the matching M release"), Slate.ProcessKeyUpEvent(MailEvent(false)));

	auto* Bag = FAccess::OpenInventory(*PC);
	if (!TestNotNull(TEXT("Inventory window"), Bag)) return false;
	TestTrue(TEXT("Inventory starts focused"), Bag->IsInViewport() && HasFocus(*Bag));
	TestTrue(TEXT("Alt+M from a focused bag closes mail and keeps the bag"),
		Slate.ProcessKeyDownEvent(MailEvent(true)) && !FAccess::IsMailOpen(*PC) && HasFocus(*Bag));
	Slate.ProcessKeyUpEvent(MailEvent(true));
	const auto TextEntry = SNew(SEditableText);
	Overlay->AddSlot()[TextEntry];
	Slate.SetUserFocus(User->GetUserIndex(), TextEntry, EFocusCause::SetDirectly);
	TestTrue(TEXT("Text input has keyboard focus"), Slate.GetUserFocusedWidget(User->GetUserIndex()) == TextEntry);
	Slate.ProcessKeyDownEvent(MailEvent(true));
	Slate.ProcessKeyUpEvent(MailEvent(true));
	TestFalse(TEXT("Alt+M leaves mail closed while editing text"), FAccess::IsMailOpen(*PC));
	Slate.SetUserFocus(User->GetUserIndex(), Bag->GetCachedWidget(), EFocusCause::SetDirectly);
	Overlay->RemoveSlot(TextEntry);
	const auto EntryActorClass = LoadClass<ARPGContentEntryActor>(nullptr,
		TEXT("/Game/Blueprints/Content/BP_SurvivalEntry.BP_SurvivalEntry_C"));
	if (!TestNotNull(TEXT("Authored content entrance class"), EntryActorClass)) return false;
	auto* EntryActor = World->SpawnActor<ARPGContentEntryActor>(EntryActorClass);
	auto* Pawn = World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Content entrance actor"), EntryActor) || !TestNotNull(TEXT("Local pawn"), Pawn)) return false;
	PC->SetPawn(Pawn);
	FAccess::Interact(*PC, *EntryActor);
	auto* Entry = FAccess::Entry(*PC);
	if (!TestNotNull(TEXT("Content entry popup"), Entry)) return false;
	TestTrue(TEXT("Content entry takes focus and selects UI-only mode"), HasFocus(*Entry) && Viewport->IgnoreInput());
	Slate.ProcessKeyDownEvent(MailEvent(true));
	Slate.ProcessKeyUpEvent(MailEvent(true));
	TestFalse(TEXT("Alt+M cannot open mail over a content entry modal"), FAccess::IsMailOpen(*PC));
	FAccess::Interact(*PC, *EntryActor);
	TestTrue(TEXT("Reopening preserves the existing content entry"), FAccess::Entry(*PC) == Entry);
	TestTrue(TEXT("Repeated Escape is handled and keeps the entry open"), Escape(true) && Entry->IsInViewport());
	const auto ConfirmationClass = LoadClass<URPGMessageBox>(nullptr,
		TEXT("/Game/Blueprints/UI/GameContents/WBP_MessageBox.WBP_MessageBox_C"));
	if (!TestNotNull(TEXT("Authored entry confirmation"), ConfirmationClass)) return false;
	TestTrue(TEXT("Entry confirmation opens"), PC->ShowContentEntryConfirmation(*Entry, ConfirmationClass, FGameplayTag()));
	auto* Confirmation = FAccess::Confirmation(*PC);
	if (!TestNotNull(TEXT("Entry confirmation widget"), Confirmation)) return false;
	TestTrue(TEXT("Confirmation receives focus above the entry popup"), HasFocus(*Confirmation));
	TestTrue(TEXT("First Escape closes only the confirmation"), Escape() && !FAccess::Confirmation(*PC) && HasFocus(*Entry));
	TestTrue(TEXT("Second Escape closes the entry and restores inventory focus"),
		Escape() && !FAccess::Entry(*PC) && Bag->IsInViewport() && HasFocus(*Bag));
	TestTrue(TEXT("Restored inventory retains movement and look locks in Game and UI mode"),
		PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored() && !Viewport->IgnoreInput());
	FAccess::Interact(*PC, *EntryActor);
	Entry = FAccess::Entry(*PC);
	if (!TestNotNull(TEXT("Content entry reopens"), Entry)) return false;
	Entry->RemoveFromParent();
	TestTrue(TEXT("Direct removal also restores the active inventory window"), !FAccess::Entry(*PC) && HasFocus(*Bag));

	auto* Equipment = FAccess::OpenEquipment(*PC);
	if (!TestNotNull(TEXT("Equipment window"), Equipment)) return false;
	TestTrue(TEXT("Equipment starts above the bag"), FAccess::Top(*PC) == Equipment);
	const auto ClickClose = [&](URPGInventoryProjectionWidget& Panel)
	{
		auto* Close = Cast<UButton>(Panel.GetWidgetFromName(TEXT("CloseWindowButton")));
		if (!TestNotNull(TEXT("Close button"), Close)) return false;
		FWidgetPath ViewportPath;
		FWidgetPath GeometryPath;
		Window->SlatePrepass();
		if (!TestTrue(TEXT("Viewport and close button have valid Slate paths"),
			Slate.GeneratePathToWidgetUnchecked(GameWidget, ViewportPath)
			&& Slate.GeneratePathToWidgetUnchecked(Close->TakeWidget(), GeometryPath))) return false;
		// Focus paths lack the per-widget pointer entries that pointer routing requires.
		TArray<FWidgetAndPointer> PointerWidgets;
		for (int32 Index = 0; Index < GeometryPath.Widgets.Num(); ++Index)
		{
			PointerWidgets.Emplace(GeometryPath.Widgets[Index], TOptional<FVirtualPointerPosition>());
		}
		const FWidgetPath ButtonPath(PointerWidgets);
		const auto ApplyPendingInputMode = [&]()
		{
			FReply& Pending = Player->GetSlateOperations();
			Slate.ProcessExternalReply(ViewportPath, Pending, User->GetUserIndex());
			Pending = FReply::Unhandled();
		};
		const FArrangedWidget& Button = ButtonPath.Widgets.Last();
		const FVector2D Point = Button.Geometry.GetAbsolutePosition() + Button.Geometry.GetAbsoluteSize() * 0.5;
		const FPointerEvent Press(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Point, Point, {EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		const FPointerEvent Release(User->GetUserIndex(), FSlateApplication::CursorPointerIndex,
			Point, Point, {}, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		ApplyPendingInputMode();
		Button.Widget->OnMouseEnter(Button.Geometry, Press);
		Slate.RoutePointerDownEvent(ButtonPath, Press);
		TestTrue(TEXT("Press brings the clicked panel to the front"), FAccess::Top(*PC) == &Panel);
		TestTrue(TEXT("Button owns the pointer after press"), Button.Widget->HasMouseCapture());
		// Slate applies queued viewport operations on a frame between press and release.
		ApplyPendingInputMode();
		TestTrue(TEXT("Button keeps its pointer capture until release"), Button.Widget->HasMouseCapture());
		Slate.RoutePointerUpEvent(ButtonPath, Release);
		Button.Widget->OnMouseLeave(Release);
		return TestFalse(TEXT("One click closes the panel"), Panel.IsActivated());
	};
	if (!ClickClose(*Bag)) return false;
	TestTrue(TEXT("Closing the bag restores equipment focus and retains the input locks"),
		HasFocus(*Equipment) && PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
	if (!ClickClose(*Equipment)) return false;
	TestTrue(TEXT("The last close restores gameplay input"),
		!FAccess::Top(*PC) && !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored());
	return true;
}

#endif
