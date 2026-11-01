#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/UI/PlayerUIComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/Package.h"
#include <limits>

namespace
{
struct FProgressFixture
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	UPlayerUIComponent* UI = nullptr;
	UObject* Source = nullptr;
	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Pawn = World->SpawnActor<APawn>();
		if (!Pawn) return false;
		UI = NewObject<UPlayerUIComponent>(Pawn);
		Pawn->AddInstanceComponent(UI);
		UI->RegisterComponent();
		Source = NewObject<USceneComponent>(Pawn);
		return UI->IsRegistered();
	}
	~FProgressFixture()
	{
		if (UI) UI->OnActionProgressChangedNative.Clear();
		RPGTestWorld::Destroy(World);
	}
};

FRPGActionProgressData Timed(ERPGActionProgressKind Kind = ERPGActionProgressKind::Charge)
{
	FRPGActionProgressData Data;
	Data.Kind = Kind; Data.Label = FText::FromString(TEXT("Action"));
	Data.bHasDuration = true; Data.DurationSeconds = 4.f; Data.ElapsedSeconds = 1.f;
	Data.TimeDisplay = ERPGActionProgressTimeDisplay::Remaining;
	return Data;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressModesTest, "ProjectRPG.UI.ActionProgress.ModesAndReadiness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressModesTest::RunTest(const FString&)
{
	FProgressFixture F; if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	TestFalse(TEXT("UI component does not enable polling"), F.UI->PrimaryComponentTick.bCanEverTick);
	for (const auto Kind : {ERPGActionProgressKind::Holding, ERPGActionProgressKind::Casting,
		ERPGActionProgressKind::Charge, ERPGActionProgressKind::Interaction})
	{
		auto Data = Timed(Kind);
		const auto Handle = F.UI->BeginActionProgress(F.Source, Data);
		TestTrue(TEXT("Every mode can begin"), Handle.IsValid());
		auto State = F.UI->GetActionProgressSnapshot();
		TestEqual(TEXT("One atomic initial snapshot includes label"), State.Data.Label.ToString(), TEXT("Action"));
		TestEqual(TEXT("Fraction comes from producer time"), State.Fraction, 0.25f);
		TestEqual(TEXT("Remaining time"), State.DisplaySeconds, 3.f);
		Data.ElapsedSeconds = 4.f;
		TestTrue(TEXT("Producer reaches full progress"), F.UI->UpdateActionProgress(Handle, Data));
		TestTrue(TEXT("100 percent never ends gameplay or display"), F.UI->GetActionProgressSnapshot().bActive);
		Data.Phase = ERPGActionProgressPhase::Ready;
		Data.Stage = 2; Data.StageCount = 2;
		TestTrue(TEXT("Ready is distinct from end"), F.UI->UpdateActionProgress(Handle, Data));
		State = F.UI->GetActionProgressSnapshot();
		TestTrue(TEXT("Can keep holding at ready"), State.bActive);
		TestEqual(TEXT("Ready phase retained"), State.Data.Phase, ERPGActionProgressPhase::Ready);
		TestTrue(TEXT("Only producer can complete"), F.UI->EndActionProgress(Handle, ERPGActionProgressEndReason::Completed));
		TestFalse(TEXT("Finished hidden"), F.UI->GetActionProgressSnapshot().bActive);
		TestFalse(TEXT("Duplicate end rejected"), F.UI->EndActionProgress(Handle, ERPGActionProgressEndReason::Completed));
	}
	auto Data = Timed(ERPGActionProgressKind::Holding);
	Data.bHasDuration = false; Data.TimeDisplay = ERPGActionProgressTimeDisplay::Elapsed;
	const auto Handle = F.UI->BeginActionProgress(F.Source, Data);
	TestTrue(TEXT("Indeterminate holding supported"), Handle.IsValid());
	TestFalse(TEXT("No invented duration"), F.UI->GetActionProgressSnapshot().Data.bHasDuration);
	TestEqual(TEXT("Elapsed time still available"), F.UI->GetActionProgressSnapshot().DisplaySeconds, 1.f);
	Data = Timed(ERPGActionProgressKind::Interaction); Data.bReverseFill = true;
	const auto Interaction = F.UI->BeginActionProgress(F.Source, Data);
	TestEqual(TEXT("Decreasing fill"), F.UI->GetActionProgressSnapshot().Fraction, 0.75f);
	Data.Phase = ERPGActionProgressPhase::AwaitingConfirmation;
	TestTrue(TEXT("Awaiting authority is display-only"), F.UI->UpdateActionProgress(Interaction, Data));
	TestTrue(TEXT("No UI clock advances or auto-finishes"), F.UI->GetActionProgressSnapshot().bActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressHandoffTest, "ProjectRPG.UI.ActionProgress.ExecutionHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressHandoffTest::RunTest(const FString&)
{
	FProgressFixture F; if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	auto Data = Timed();
	const auto A = F.UI->BeginActionProgress(F.Source, Data);
	Data.Label = FText::FromString(TEXT("Second execution")); Data.ElapsedSeconds = 0.f;
	const auto B = F.UI->BeginActionProgress(F.Source, Data);
	TestFalse(TEXT("Same source has a fresh per-execution identity"), A == B);
	TestFalse(TEXT("Late A update rejected"), F.UI->UpdateActionProgress(A, Timed()));
	TestFalse(TEXT("Late A end cannot hide B"), F.UI->EndActionProgress(A, ERPGActionProgressEndReason::Cancelled));
	const auto LateSubscriber = F.UI->GetActionProgressSnapshot();
	TestTrue(TEXT("Late UI attachment can read current action"), LateSubscriber.Handle == B);
	TestEqual(TEXT("No old progress leaks into new action"), LateSubscriber.Fraction, 0.f);
	bool bNested = false;
	FRPGActionProgressHandle Inner;
	F.UI->OnActionProgressChangedNative.AddLambda([&]
	{
		if (!bNested) { bNested = true; Inner = F.UI->BeginActionProgress(F.Source, Data); }
	});
	const auto Outer = F.UI->BeginActionProgress(F.Source, Data);
	TestTrue(TEXT("Nested begin returned a valid handle"), Inner.IsValid());
	TestFalse(TEXT("Outer caller never receives inner execution identity"), Outer == Inner);
	TestTrue(TEXT("Reentrant notification does not overwrite new state"), F.UI->GetActionProgressSnapshot().Handle == Inner);
	TestFalse(TEXT("Outer delayed callback cannot end inner action"), F.UI->EndActionProgress(Outer, ERPGActionProgressEndReason::Interrupted));
	F.UI->OnActionProgressChangedNative.Clear();
	F.UI->OnActionProgressChangedNative.AddLambda([&]
	{
		if (!F.UI->GetActionProgressSnapshot().bActive) Inner = F.UI->BeginActionProgress(F.Source, Data);
	});
	TestTrue(TEXT("End can notify a new begin"), F.UI->EndActionProgress(Inner, ERPGActionProgressEndReason::Cancelled));
	TestTrue(TEXT("End cleanup never clears reentrant new action"), F.UI->GetActionProgressSnapshot().bActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressLifetimeTest, "ProjectRPG.UI.ActionProgress.SourceAndOwnerLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressLifetimeTest::RunTest(const FString&)
{
	FProgressFixture F; if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	const auto Data = Timed();
	const auto A = F.UI->BeginActionProgress(F.Source, Data);
	F.Source->MarkAsGarbage();
	TestFalse(TEXT("Destroyed source is hidden on read without polling"), F.UI->GetActionProgressSnapshot().bActive);
	TestFalse(TEXT("Dead source cannot update"), F.UI->UpdateActionProgress(A, Data));
	F.Source = NewObject<USceneComponent>(F.Pawn);
	const auto B = F.UI->BeginActionProgress(F.Source, Data);
	int32 LostEvents = 0;
	F.UI->OnActionProgressChangedNative.AddLambda([&]
	{
		if (F.UI->GetActionProgressSnapshot().EndReason == ERPGActionProgressEndReason::SourceLost) ++LostEvents;
	});
	auto* Controller = F.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Controller created"), Controller)) return false;
	Controller->Possess(F.Pawn);
	TestFalse(TEXT("Possession change invalidates old UI immediately"), F.UI->GetActionProgressSnapshot().bActive);
	TestEqual(TEXT("Controller event pushed invalidation without Tick"), LostEvents, 1);
	TestFalse(TEXT("Old controller scope cannot update"), F.UI->UpdateActionProgress(B, Data));
	const auto C = F.UI->BeginActionProgress(F.Source, Data);
	Controller->UnPossess();
	Controller->Possess(F.Pawn);
	TestFalse(TEXT("Same controller returning does not resurrect a handle"), F.UI->UpdateActionProgress(C, Data));
	const auto D = F.UI->BeginActionProgress(F.Source, Data);
	F.UI->UnregisterComponent();
	TestFalse(TEXT("Unregistered component hidden"), F.UI->GetActionProgressSnapshot().bActive);
	TestFalse(TEXT("Cannot begin while unregistered"), F.UI->BeginActionProgress(F.Source, Data).IsValid());
	F.UI->RegisterComponent();
	TestFalse(TEXT("Re-register cannot reuse old handle"), F.UI->UpdateActionProgress(D, Data));
	TestTrue(TEXT("New registration supports a new action"), F.UI->BeginActionProgress(F.Source, Data).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressInvalidInputTest, "ProjectRPG.UI.ActionProgress.InvalidInputPreservesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressInvalidInputTest::RunTest(const FString&)
{
	FProgressFixture F; if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	auto Data = Timed();
	const auto Handle = F.UI->BeginActionProgress(F.Source, Data);
	int32 Notifications = 0;
	F.UI->OnActionProgressChangedNative.AddLambda([&] { ++Notifications; });
	TestFalse(TEXT("Null source cannot replace current display"), F.UI->BeginActionProgress(nullptr, Data).IsValid());
	for (const float BadDuration : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		Data.DurationSeconds = BadDuration;
		TestFalse(TEXT("Invalid duration rejected"), F.UI->UpdateActionProgress(Handle, Data));
	}
	Data = Timed(); Data.ElapsedSeconds = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN elapsed rejected"), F.UI->UpdateActionProgress(Handle, Data));
	Data = Timed(); Data.Kind = ERPGActionProgressKind::Casting;
	TestFalse(TEXT("Update cannot change action kind"), F.UI->UpdateActionProgress(Handle, Data));
	Data = Timed(); Data.Stage = 3; Data.StageCount = 2;
	TestFalse(TEXT("Invalid stage rejected"), F.UI->UpdateActionProgress(Handle, Data));
	Data = Timed(); Data.bHasDuration = false;
	TestFalse(TEXT("Unknown remaining time is not invented"), F.UI->UpdateActionProgress(Handle, Data));
	TestFalse(TEXT("Empty identity rejected"), F.UI->EndActionProgress({}, ERPGActionProgressEndReason::Cancelled));
	TestFalse(TEXT("No terminal reason rejected"), F.UI->EndActionProgress(Handle, ERPGActionProgressEndReason::None));
	TestEqual(TEXT("Rejected requests do not notify or mutate state"), Notifications, 0);
	TestTrue(TEXT("Current identity preserved"), F.UI->GetActionProgressSnapshot().Handle == Handle);
	TestEqual(TEXT("Current value preserved"), F.UI->GetActionProgressSnapshot().Fraction, 0.25f);
	Data = Timed(); Data.ElapsedSeconds = 100.f;
	TestTrue(TEXT("Finite overtime accepted as presentation"), F.UI->UpdateActionProgress(Handle, Data));
	TestEqual(TEXT("Fraction is clamped"), F.UI->GetActionProgressSnapshot().Fraction, 1.f);
	TestEqual(TEXT("Remaining time never negative"), F.UI->GetActionProgressSnapshot().DisplaySeconds, 0.f);
	TestTrue(TEXT("Overtime still does not auto-complete"), F.UI->GetActionProgressSnapshot().bActive);
	return true;
}
#endif
