#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UI/RPGWidgetBase.h"
#include "UI/RPGWindowInputState.h"
#include "UObject/StrongObjectPtr.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SOverlay.h"

namespace RPGWindowInputStateTests
{
struct FFixture
{
	UWorld* World = nullptr;
	APlayerController* Controller = nullptr;
	TStrongObjectPtr<URPGWidgetBase> Skill{NewObject<URPGWidgetBase>()};
	TStrongObjectPtr<URPGWidgetBase> Inventory{NewObject<URPGWidgetBase>()};
	TStrongObjectPtr<URPGWidgetBase> Equipment{NewObject<URPGWidgetBase>()};

	FFixture()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
		if (!World) return;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Controller = World->SpawnActor<APlayerController>();
		Skill->Initialize();
		Inventory->Initialize();
		Equipment->Initialize();
	}
	~FFixture()
	{
		if (!World) return;
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGCoexistingWindowInputTest,
	"ProjectRPG.Skill.UI.CoexistingWindows.InputLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGCoexistingWindowInputTest::RunTest(const FString&)
{
	using namespace RPGWindowInputStateTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Local input fixture controller"), Fixture.Controller)) return false;
	APlayerController& PC = *Fixture.Controller;
	// Both opening orders, both closing orders, prior cursor and independent locks.
	for (const bool bSkillFirst : {false, true})
	for (const bool bCloseFirstOpened : {false, true})
	for (const bool bPriorCursor : {false, true})
	for (const bool bPriorMoveLock : {false, true})
	for (const bool bPriorLookLock : {false, true})
	{
		PC.SetShowMouseCursor(bPriorCursor);
		if (bPriorMoveLock) PC.SetIgnoreMoveInput(true);
		if (bPriorLookLock) PC.SetIgnoreLookInput(true);
		FRPGWindowInputState State;
		int32 ReleaseHeldInputCount = 0;
		State.OnFirstWindowOpened.AddLambda([&]() { ++ReleaseHeldInputCount; });
		URPGWidgetBase& First = *(bSkillFirst ? Fixture.Skill : Fixture.Inventory);
		URPGWidgetBase& Second = *(bSkillFirst ? Fixture.Inventory : Fixture.Skill);
		TestTrue(TEXT("First window registers"), State.BeginWindow(PC, First));
		TestTrue(TEXT("Duplicate registration is idempotent"), State.BeginWindow(PC, First));
		TestTrue(TEXT("Second independent window registers"), State.BeginWindow(PC, Second));
		TestEqual(TEXT("Held inputs are released once per shared session"), ReleaseHeldInputCount, 1);
		TestTrue(TEXT("Cursor remains visible with two windows"), PC.bShowMouseCursor);
		TestTrue(TEXT("Movement stays locked"), PC.IsMoveInputIgnored());
		TestTrue(TEXT("Camera stays locked"), PC.IsLookInputIgnored());
		URPGWidgetBase& FirstClosed = bCloseFirstOpened ? First : Second;
		URPGWidgetBase& LastClosed = bCloseFirstOpened ? Second : First;
		State.EndWindow(FirstClosed);
		State.EndWindow(FirstClosed);
		TestTrue(TEXT("Closing one window retains the other session"), State.HasOpenWindows());
		TestTrue(TEXT("Closing one window retains cursor"), PC.bShowMouseCursor);
		TestTrue(TEXT("Closing one window retains movement lock"), PC.IsMoveInputIgnored());
		TestTrue(TEXT("Closing one window retains camera lock"), PC.IsLookInputIgnored());
		State.EndWindow(LastClosed);
		State.EndWindow(LastClosed);
		TestFalse(TEXT("Last close ends the session"), State.HasOpenWindows());
		TestEqual(TEXT("Last close restores original cursor"), PC.bShowMouseCursor, bPriorCursor);
		TestEqual(TEXT("Last close preserves external movement locks"), PC.IsMoveInputIgnored(), bPriorMoveLock);
		TestEqual(TEXT("Last close preserves external camera locks"), PC.IsLookInputIgnored(), bPriorLookLock);
		if (bPriorMoveLock) PC.SetIgnoreMoveInput(false);
		if (bPriorLookLock) PC.SetIgnoreLookInput(false);
		TestFalse(TEXT("No movement lock leaks after balancing the external lock"), PC.IsMoveInputIgnored());
		TestFalse(TEXT("No camera lock leaks after balancing the external lock"), PC.IsLookInputIgnored());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGWindowInputReplacementTest,
	"ProjectRPG.Skill.UI.CoexistingWindows.ReplacementAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGWindowInputReplacementTest::RunTest(const FString&)
{
	using namespace RPGWindowInputStateTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Input fixture controller"), Fixture.Controller)) return false;
	APlayerController& PC = *Fixture.Controller;
	PC.SetShowMouseCursor(false);
	PC.SetIgnoreMoveInput(true);
	FRPGWindowInputState State;
	int32 OpenCount = 0;
	State.OnFirstWindowOpened.AddLambda([&]() { ++OpenCount; });
	State.BeginWindow(PC, *Fixture.Skill);
	State.BeginWindow(PC, *Fixture.Inventory);
	State.BeginWindow(PC, *Fixture.Equipment);
	State.EndWindow(*Fixture.Inventory);
	TestEqual(TEXT("Tab replacement does not start another input session"), OpenCount, 1);
	State.EndWindow(*Fixture.Skill);
	TestTrue(TEXT("Equipment remains open after the skill window closes"), State.HasOpenWindows());
	TestTrue(TEXT("Equipment retains camera lock"), PC.IsLookInputIgnored());
	State.Reset();
	State.Reset();
	TestFalse(TEXT("Teardown clears all registered windows"), State.HasOpenWindows());
	TestFalse(TEXT("Teardown restores cursor"), PC.bShowMouseCursor);
	TestTrue(TEXT("Teardown retains external movement lock"), PC.IsMoveInputIgnored());
	TestFalse(TEXT("Teardown balances camera lock"), PC.IsLookInputIgnored());
	PC.SetIgnoreMoveInput(false);
	TestFalse(TEXT("Teardown does not leak a movement lock"), PC.IsMoveInputIgnored());
	// A held-input release callback can cancel opening synchronously.
	State.OnFirstWindowOpened.Clear();
	State.OnFirstWindowOpened.AddLambda([&]() { State.EndWindow(*Fixture.Skill); });
	TestFalse(TEXT("Reentrant close cancels opening"), State.BeginWindow(PC, *Fixture.Skill));
	TestFalse(TEXT("Reentrant close leaves no open window"), State.HasOpenWindows());
	TestFalse(TEXT("Reentrant close leaves no cursor or input locks"),
		PC.bShowMouseCursor || PC.IsMoveInputIgnored() || PC.IsLookInputIgnored());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIndependentWindowStackTest,
	"ProjectRPG.Item.UI.IndependentWindows.StackAndInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGIndependentWindowStackTest::RunTest(const FString&)
{
	using namespace RPGWindowInputStateTests;
	FFixture Fixture;
	if (!TestNotNull(TEXT("Window controller"), Fixture.Controller)) return false;
	FRPGWindowInputState State;
	const auto Host = [](URPGWidgetBase& Window) -> TSharedRef<SConstraintCanvas>
	{
		return SNew(SConstraintCanvas) + SConstraintCanvas::Slot()[Window.TakeWidget()];
	};
	const auto BagHost = Host(*Fixture.Inventory);
	const auto EquipmentHost = Host(*Fixture.Equipment);
	const auto SkillHost = Host(*Fixture.Skill);
	const TSharedRef<SOverlay> Overlay = SNew(SOverlay)
		+ SOverlay::Slot(30)[BagHost]
		+ SOverlay::Slot(31)[EquipmentHost]
		+ SOverlay::Slot(32)[SkillHost];
	State.BeginWindow(*Fixture.Controller, *Fixture.Inventory);
	State.BeginWindow(*Fixture.Controller, *Fixture.Equipment);
	State.BeginWindow(*Fixture.Controller, *Fixture.Skill);
	Fixture.Inventory->SetRenderTranslation(FVector2D(40.0, 20.0));
	State.FocusWindow(*Fixture.Inventory);
	TestTrue(TEXT("Clicking the bag makes it the top registered window"), State.GetTopWindow() == Fixture.Inventory.Get());
	TestTrue(TEXT("The real viewport overlay, not only its inner canvas, is reordered"),
		Overlay->GetChildren()->GetChildAt(2) == BagHost);
	TestTrue(TEXT("Raising a window preserves the existing controls"),
		Fixture.Inventory->GetCachedWidget()->GetParentWidget() == BagHost);
	TestTrue(TEXT("Raising preserves the user's dragged position"),
		Fixture.Inventory->GetRenderTransform().Translation.Equals(FVector2D(40.0, 20.0)));
	TestTrue(TEXT("Other independent windows remain registered"),
		State.ContainsWindow(*Fixture.Equipment) && State.ContainsWindow(*Fixture.Skill));

	// Modal priority is independent of which normal window was clicked last.
	TStrongObjectPtr<URPGWidgetBase> Modal(NewObject<URPGWidgetBase>());
	Modal->Initialize();
	const auto ModalHost = Host(*Modal);
	Overlay->AddSlot(130)[ModalHost];
	State.BeginWindow(*Fixture.Controller, *Modal, 120);
	State.FocusWindow(*Fixture.Equipment);
	TestTrue(TEXT("A normal click cannot displace modal focus"), State.GetTopWindow() == Modal.Get());
	TestTrue(TEXT("Modal remains above the normal window overlay slots"),
		Overlay->GetChildren()->GetChildAt(3) == ModalHost);
	State.EndWindow(*Modal);
	TestTrue(TEXT("Closing modal returns to the most recently selected normal window"),
		State.GetTopWindow() == Fixture.Equipment.Get());
	State.EndWindow(*Fixture.Equipment);
	TestTrue(TEXT("Closing one window restores the previous window"), State.GetTopWindow() == Fixture.Inventory.Get());
	TestTrue(TEXT("Remaining windows keep the cursor and gameplay lock"),
		Fixture.Controller->bShowMouseCursor && Fixture.Controller->IsMoveInputIgnored());
	State.EndWindow(*Fixture.Inventory);
	State.EndWindow(*Fixture.Skill);
	TestTrue(TEXT("Only the last close restores gameplay input"),
		!State.HasOpenWindows() && !Fixture.Controller->bShowMouseCursor
		&& !Fixture.Controller->IsMoveInputIgnored() && !Fixture.Controller->IsLookInputIgnored());
	return true;
}

#endif
