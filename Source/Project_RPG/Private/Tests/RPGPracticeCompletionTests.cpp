#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameMode/RPGIntroApproachGameMode.h"
#include "GameMode/RPGTrainingGameMode.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"

struct FRPGPracticeCompletionTestAccess
{
	static FString Content(const ARPGTrainingGameMode& Mode) { return Mode.GetPracticeContentId(); }
	static int32 Status(const ARPGTrainingGameMode& Mode, const UActorComponent* Objectives)
	{
		const auto Value = Mode.ReadPracticeStatus(Objectives);
		return (Value.bActive ? 1 : 0) | (Value.bEmpty ? 2 : 0) | (Value.bComplete ? 4 : 0);
	}
	static void Bind(ARPGTrainingGameMode& Mode, UActorComponent* Objectives) { Mode.BindPracticeObjectives(Objectives); }
	static void Unbind(ARPGTrainingGameMode& Mode, UActorComponent* Objectives) { Mode.UnbindPracticeObjectives(Objectives); }
	static void ObserveUntrusted(ARPGTrainingGameMode& Mode, UActorComponent* Objectives)
	{
		ARPGTrainingGameMode::FMemberObservation Observation;
		Observation.Objectives = Objectives;
		Observation.CompletedAtSeconds = 123.0;
		Mode.Observations = {Observation};
		Mode.NotifyPracticeProgressChanged();
	}
	static double CompletionTime(const ARPGTrainingGameMode& Mode) { return Mode.Observations[0].CompletedAtSeconds; }
	static bool Submitted(const ARPGTrainingGameMode& Mode) { return Mode.bCompletionSubmitted; }
};

namespace
{
	using Access = FRPGPracticeCompletionTestAccess;
	template<typename T> void SetReplica(UActorComponent& Component, const T& State)
	{
		auto* Property = FindFProperty<FStructProperty>(Component.GetClass(), TEXT("Progress"));
		check(Property); *Property->ContainerPtrToValuePtr<T>(&Component) = State;
	}
	struct FFixture
	{
		UWorld* World = nullptr;
		ARPGTrainingGameMode* Training = nullptr;
		ARPGIntroApproachGameMode* Intro = nullptr;
		URPGTrainingObjectiveComponent* TrainingGoals = nullptr;
		URPGIntroApproachObjectiveComponent* IntroGoals = nullptr;
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Training = World->SpawnActor<ARPGTrainingGameMode>(); Intro = World->SpawnActor<ARPGIntroApproachGameMode>();
			auto* Pawn = World->SpawnActor<APawn>();
			if (!Training || !Intro || !Pawn) return false;
			TrainingGoals = NewObject<URPGTrainingObjectiveComponent>(Pawn);
			IntroGoals = NewObject<URPGIntroApproachObjectiveComponent>(Pawn);
			Pawn->AddInstanceComponent(TrainingGoals); TrainingGoals->RegisterComponent();
			Pawn->AddInstanceComponent(IntroGoals); IntroGoals->RegisterComponent();
			return true;
		}
		~FFixture() { if (World) RPGTestWorld::Destroy(World); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPracticeContentAdaptersTest,
	"ProjectRPG.Progression.PracticeCompletion.ContentAdapters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPracticeContentAdaptersTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Isolated content adapters"), F.Initialize())) return false;
	TestEqual(TEXT("Existing training keeps its exact content ID"), Access::Content(*F.Training), FString(TEXT("Progression.Training")));
	TestEqual(TEXT("Intro1 has a separate exact content ID"), Access::Content(*F.Intro), FString(TEXT("Progression.Intro1")));
	for (int32 Mask = 0; Mask < 16; ++Mask)
	{
		FRPGTrainingObjectiveProgress P;
		P.bActive = true; P.bBasicAttackCompleted = !!(Mask & 1); P.bEvadeCompleted = !!(Mask & 2);
		P.bAssaultCompleted = !!(Mask & 4); P.bChargeCompleted = !!(Mask & 8);
		SetReplica(*F.TrainingGoals, P);
		const int32 Expected = 1 | (Mask == 0 ? 2 : 0) | (Mask == 15 ? 4 : 0);
		TestEqual(TEXT("Training preserves all sixteen old partial/full states"), Access::Status(*F.Training, F.TrainingGoals), Expected);
		TestEqual(TEXT("Intro1 cannot read a training receipt"), Access::Status(*F.Intro, F.TrainingGoals), 0);
	}
	for (int32 Mask = 0; Mask < 8; ++Mask)
	{
		FRPGIntroApproachProgress P;
		P.bActive = true; P.bStartReached = !!(Mask & 1); P.bApproachCompleted = !!(Mask & 2); P.bAttackCompleted = !!(Mask & 4);
		SetReplica(*F.IntroGoals, P);
		TestEqual(TEXT("Intro1 requires its own complete three-step state"), Access::Status(*F.Intro, F.IntroGoals), 1 | (Mask == 0 ? 2 : 0) | (Mask == 7 ? 4 : 0));
		TestEqual(TEXT("Training cannot read an Intro1 receipt"), Access::Status(*F.Training, F.IntroGoals), 0);
	}
	FRPGIntroApproachProgress Inactive; Inactive.bStartReached = Inactive.bApproachCompleted = Inactive.bAttackCompleted = true;
	SetReplica(*F.IntroGoals, Inactive);
	TestEqual(TEXT("Inactive stale completion is neither active nor complete"), Access::Status(*F.Intro, F.IntroGoals), 0);
	TestEqual(TEXT("Missing component fails closed"), Access::Status(*F.Intro, nullptr), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPracticeDelegateLifetimeTest,
	"ProjectRPG.Progression.PracticeCompletion.DelegateLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPracticeDelegateLifetimeTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Typed delegate fixture"), F.Initialize())) return false;
	Access::Bind(*F.Training, F.IntroGoals); Access::Bind(*F.Intro, F.TrainingGoals);
	TestFalse(TEXT("Wrong source does not bind training"), F.TrainingGoals->OnTrainingProgressChanged.IsBound());
	TestFalse(TEXT("Wrong source does not bind Intro1"), F.IntroGoals->OnApproachProgressChanged.IsBound());
	Access::Bind(*F.Training, F.TrainingGoals); Access::Bind(*F.Intro, F.IntroGoals);
	TestTrue(TEXT("Training binds its established event"), F.TrainingGoals->OnTrainingProgressChanged.IsBound());
	TestTrue(TEXT("Intro1 binds its own server event"), F.IntroGoals->OnApproachProgressChanged.IsBound());
	F.TrainingGoals->OnTrainingProgressChanged.Broadcast({}); F.IntroGoals->OnApproachProgressChanged.Broadcast({});
	TestFalse(TEXT("A training event is not a settlement receipt"), Access::Submitted(*F.Training));
	TestFalse(TEXT("An Intro1 event is not a settlement receipt"), Access::Submitted(*F.Intro));
	Access::Unbind(*F.Training, F.TrainingGoals); Access::Unbind(*F.Intro, F.IntroGoals);
	TestFalse(TEXT("Training unbinds its event"), F.TrainingGoals->OnTrainingProgressChanged.IsBound());
	TestFalse(TEXT("Intro1 unbinds its event"), F.IntroGoals->OnApproachProgressChanged.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGPracticeUntrustedCompletionTest,
	"ProjectRPG.Progression.PracticeCompletion.UntrustedCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGPracticeUntrustedCompletionTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Unprivileged completion fixture"), F.Initialize())) return false;
	FRPGIntroApproachProgress P; P.bActive = P.bStartReached = P.bApproachCompleted = P.bAttackCompleted = true;
	SetReplica(*F.IntroGoals, P); // Display-only fixture, no authenticated membership or Pawn.
	Access::ObserveUntrusted(*F.Intro, F.IntroGoals);
	TestEqual(TEXT("An untrusted completed bool cannot retain a completion timestamp"), Access::CompletionTime(*F.Intro), 0.0);
	F.Intro->ReportConfiguredDungeonClear(); F.Training->ReportConfiguredDungeonClear();
	TestFalse(TEXT("The public Intro1 clear entry still passes the common server gate"), Access::Submitted(*F.Intro));
	TestFalse(TEXT("The original public training clear entry remains gated"), Access::Submitted(*F.Training));
	Access::Unbind(*F.Intro, F.IntroGoals);
	return true;
}
#endif
