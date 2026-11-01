#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"

struct FRPGIntroApproachObjectiveTestAccess
{
	static bool Capture(URPGIntroApproachObjectiveComponent& C) { return C.FindZones(C.Zones); }
	static bool Current(const URPGIntroApproachObjectiveComponent& C) { return C.IsLayoutCurrent(); }
	static bool Contains(const URPGIntroApproachObjectiveComponent& C, int32 Index, FVector Position)
	{ return C.Zones.IsValidIndex(Index) && C.Contains(C.Zones[Index], Position); }
	static void PrimeSpatialOnly(URPGIntroApproachObjectiveComponent& C)
	{ C.Progress = {}; C.Progress.bActive = C.Progress.bStartReached = true; }
	static bool SpatialOnly(URPGIntroApproachObjectiveComponent& C, bool bApproach, FVector Start, FVector End, bool bCommitted = true)
	{
		URPGIntroApproachObjectiveComponent::FObservedCast Record;
		Record.Goal = bApproach ? URPGIntroApproachObjectiveComponent::EGoal::Approach : URPGIntroApproachObjectiveComponent::EGoal::Attack;
		Record.Start = Start; Record.bCommitted = bCommitted;
		return C.CompleteSpatialGoal(Record, End);
	}
	static void Refresh(URPGIntroApproachObjectiveComponent& C) { C.RefreshObservation(); }
	static bool AuthorityCurrent(const URPGIntroApproachObjectiveComponent& C) { return C.IsObservationCurrent(); }
	static void Clear(URPGIntroApproachObjectiveComponent& C) { C.ClearObservation(); }
};

namespace
{
	using Access = FRPGIntroApproachObjectiveTestAccess;
	const FName Tags[] = {TEXT("Progression.Intro1.Zone.Start"), TEXT("Progression.Intro1.Zone.Approach"), TEXT("Progression.Intro1.Zone.Attack")};
	const FVector Centres[] = {FVector(-2400, 0, 140), FVector(300, 0, 140), FVector(1700, 0, 140)};
	const FVector Extents[] = {FVector(650, 650, 140), FVector(400, 400, 140), FVector(700, 700, 140)};
	struct FFixture
	{
		UWorld* World = nullptr;
		APawn* Pawn = nullptr;
		URPGIntroApproachObjectiveComponent* Objectives = nullptr;
		TArray<ATriggerBox*> Boxes;
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Pawn = World->SpawnActor<APawn>();
			if (!Pawn) return false;
			Objectives = NewObject<URPGIntroApproachObjectiveComponent>(Pawn);
			Pawn->AddInstanceComponent(Objectives); Objectives->RegisterComponent();
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
			{
				auto* Box = World->SpawnActor<ATriggerBox>(Centres[Index], FRotator::ZeroRotator);
				if (!Box) return false;
				Box->Tags.Add(Tags[Index]);
				auto* Collision = CastChecked<UBoxComponent>(Box->GetCollisionComponent());
				Collision->SetBoxExtent(Extents[Index]); Collision->SetCollisionProfileName(TEXT("Trigger"));
				Boxes.Add(Box);
			}
			return Access::Capture(*Objectives);
		}
		~FFixture() { if (World) RPGTestWorld::Destroy(World); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroApproachLayoutTest,
	"ProjectRPG.Progression.Intro1.LayoutAndReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroApproachLayoutTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Isolated three-zone arena"), F.Initialize())) return false;
	TestTrue(TEXT("Captured authored layout is current"), Access::Current(*F.Objectives));
	TestTrue(TEXT("Centre and exact horizontal boundary count"), Access::Contains(*F.Objectives, 1, FVector(700, 0, 140)));
	TestFalse(TEXT("Outside horizontal extent is excluded"), Access::Contains(*F.Objectives, 1, FVector(701, 0, 140)));
	TestFalse(TEXT("Above the three-dimensional zone is excluded"), Access::Contains(*F.Objectives, 1, FVector(300, 0, 281)));
	F.Boxes[1]->SetActorLocation(FVector(350, 0, 140));
	TestFalse(TEXT("Moving an anchor invalidates a cast's captured layout"), Access::Current(*F.Objectives));
	F.Boxes[1]->SetActorLocation(Centres[1]);
	TestTrue(TEXT("Restoring the unchanged authored layout"), Access::Current(*F.Objectives));
	auto* Duplicate = F.World->SpawnActor<ATriggerBox>(); Duplicate->Tags.Add(Tags[1]);
	TestFalse(TEXT("Duplicate zone ID cannot silently choose an actor"), Access::Current(*F.Objectives));
	Duplicate->Destroy();
	F.Boxes[0]->Tags.Add(Tags[1]);
	TestFalse(TEXT("Two zone IDs cannot alias the same actor"), Access::Capture(*F.Objectives));
	F.Boxes[0]->Tags.Remove(Tags[1]);
	auto* Collision = CastChecked<UBoxComponent>(F.Boxes[1]->GetCollisionComponent());
	Collision->SetBoxExtent(FVector::ZeroVector);
	TestFalse(TEXT("A zero-volume goal is rejected"), Access::Capture(*F.Objectives));
	Collision->SetBoxExtent(Extents[1]); Collision->SetCollisionProfileName(TEXT("BlockAll"));
	TestFalse(TEXT("A blocking solid is not a goal marker"), Access::Capture(*F.Objectives));
	Collision->SetCollisionProfileName(TEXT("Trigger"));
	F.Boxes[2]->SetActorRotation(FRotator(0, 90, 0)); F.Boxes[2]->SetActorScale3D(FVector(2, .5, 1));
	TestTrue(TEXT("An authored rotated/scaled goal can be captured"), Access::Capture(*F.Objectives));
	TestTrue(TEXT("Containment uses component transform"), Access::Contains(*F.Objectives, 2, FVector(1700, 1300, 140)));
	TestFalse(TEXT("AABB false positive is excluded by local coordinates"), Access::Contains(*F.Objectives, 2, FVector(2200, 0, 140)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroApproachSequenceTest,
	"ProjectRPG.Progression.Intro1.SpatialSequence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroApproachSequenceTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Isolated spatial gate fixture"), F.Initialize())) return false;
	auto& C = *F.Objectives;
	TestFalse(TEXT("Inactive display state cannot complete a step"), Access::SpatialOnly(C, true, FVector(-300, 0, 140), Centres[1]));
	Access::PrimeSpatialOnly(C); // Bypasses authentication ONLY to test the private spatial gate.
	TestFalse(TEXT("Attack cannot precede approach"), Access::SpatialOnly(C, false, Centres[2], Centres[2]));
	TestFalse(TEXT("An uncommitted action cannot complete approach"), Access::SpatialOnly(C, true, FVector(-300, 0, 140), Centres[1], false));
	TestFalse(TEXT("Casting while already inside does not teach approach"), Access::SpatialOnly(C, true, Centres[1], Centres[1]));
	TestFalse(TEXT("Ending outside the approach zone is insufficient"), Access::SpatialOnly(C, true, FVector(-300, 0, 140), FVector(-101, 0, 140)));
	TestFalse(TEXT("A boundary crossing of less than one metre is insufficient"), Access::SpatialOnly(C, true, FVector(-101, 0, 140), FVector(-99, 0, 140)));
	TestTrue(TEXT("A committed outside-to-inside approach advances once"), Access::SpatialOnly(C, true, FVector(-300, 0, 140), Centres[1]));
	TestFalse(TEXT("Approach alone is incomplete"), C.IsApproachPracticeComplete());
	TestFalse(TEXT("A duplicate approach cannot advance the attack step"), Access::SpatialOnly(C, true, FVector(-300, 0, 140), Centres[1]));
	TestFalse(TEXT("An attack starting outside cannot be moved inside to claim it"), Access::SpatialOnly(C, false, FVector(900, 0, 140), Centres[2]));
	TestFalse(TEXT("An attack ending outside does not finish the task"), Access::SpatialOnly(C, false, Centres[2], FVector(2500, 0, 140)));
	TestTrue(TEXT("An attack starting and ending within its zone finishes spatial requirements"), Access::SpatialOnly(C, false, Centres[2], Centres[2]));
	TestTrue(TEXT("All three flags are required"), C.IsApproachPracticeComplete());
	TestFalse(TEXT("A repeated completion is ignored"), Access::SpatialOnly(C, false, Centres[2], Centres[2]));
	Access::Clear(C);
	TestFalse(TEXT("Lifetime reset removes old completion"), C.IsApproachPracticeComplete());
	TestFalse(TEXT("Lifetime reset cannot reuse its old spatial layout"), Access::Current(C));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroApproachAuthoritySurfaceTest,
	"ProjectRPG.Progression.Intro1.AuthorityAndReadOnlySurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroApproachAuthoritySurfaceTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Unprivileged standalone Pawn"), F.Initialize())) return false;
	Access::Refresh(*F.Objectives);
	TestFalse(TEXT("Tags and authority role without authenticated Intro1 cannot arm goals"), F.Objectives->GetApproachProgress().bActive);
	TestFalse(TEXT("A standalone fixture is not a current server observation"), Access::AuthorityCurrent(*F.Objectives));
	Access::PrimeSpatialOnly(*F.Objectives);
	Access::Refresh(*F.Objectives);
	TestFalse(TEXT("Untrusted prior display state is cleared by normal observation refresh"), F.Objectives->GetApproachProgress().bActive);
	const auto* ProgressProperty = FindFProperty<FStructProperty>(F.Objectives->GetClass(), TEXT("Progress"));
	if (!TestNotNull(TEXT("Replicated progress property"), ProgressProperty)) return false;
	TestTrue(TEXT("The property replicates"), ProgressProperty->HasAnyPropertyFlags(CPF_Net));
	TestFalse(TEXT("Blueprint cannot write the canonical record"), ProgressProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	for (TFieldIterator<UFunction> It(F.Objectives->GetClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		TestFalse(TEXT("The component exposes no completion RPC"), It->HasAnyFunctionFlags(FUNC_Net));
	TestEqual(TEXT("Reflected layout agrees with current native allocation"), F.Objectives->GetClass()->GetPropertiesSize(), static_cast<int32>(sizeof(URPGIntroApproachObjectiveComponent)));
	return true;
}
#endif
