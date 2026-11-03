#if WITH_DEV_AUTOMATION_TESTS

#include "AI/RPGBossTelegraphShape.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossTelegraphPresentationComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossTelegraphFootprintTest,
	"ProjectRPG.Boss.TelegraphShapes.FootprintsMatchHitQuery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossTelegraphFootprintTest::RunTest(const FString&)
{
	FRPGBossTelegraphGeometry Geometry;
	Geometry.bEnabled = true;
	Geometry.QueryTransform = FTransform(FRotator(0.f, 90.f, 0.f), FVector(100.f, 200.f, 50.f));
	Geometry.Shape.Type = ERPGHitQueryShape::RingSector;
	Geometry.Shape.Radius = 400.f;
	Geometry.Shape.InnerRadius = 150.f;
	Geometry.Shape.HalfHeight = 100.f;
	Geometry.Shape.AngleDegrees = 90.f;
	Geometry.Shape.LocalOffset = FVector(50.f, 0.f, 0.f);
	FRPGBossTelegraphFootprint Footprint;
	if (!TestTrue(TEXT("Ring sector builds"), FRPGBossTelegraphFootprint::Build(Geometry, Footprint))) return false;
	TestTrue(TEXT("Local offset uses frozen aim yaw"), Footprint.Center.Equals(FVector(100.f, 250.f, 50.f), .01));
	TestTrue(TEXT("Decal forward aligns with shape"), FMath::IsNearlyEqual(Footprint.DecalRotation.Yaw, 90.f));
	const auto Inside = [&](FVector Local) { return FRPGHitQueryMath::IsPointInsideShape(
		Footprint.ShapeTransform, Footprint.Shape, Footprint.ShapeTransform.TransformPosition(Local)); };
	TestFalse(TEXT("Donut center remains safe"), Inside(FVector(0.f, 0.f, 0.f)));
	TestFalse(TEXT("Inner radius remains safe"), Inside(FVector(100.f, 0.f, 0.f)));
	TestTrue(TEXT("Forward ring is harmful"), Inside(FVector(250.f, 0.f, 0.f)));
	TestFalse(TEXT("Back and sector outside remain safe"), Inside(FVector(-250.f, 0.f, 0.f)));
	Geometry.Shape.AngleDegrees = 360.f;
	FRPGBossTelegraphFootprint::Build(Geometry, Footprint);
	TestTrue(TEXT("360 degrees includes rear ring"), Inside(FVector(-250.f, 0.f, 0.f)));
	Geometry.Shape.Type = ERPGHitQueryShape::Box;
	Geometry.Shape.BoxHalfExtent = FVector(400.f, 80.f, 100.f);
	FRPGBossTelegraphFootprint::Build(Geometry, Footprint);
	TestTrue(TEXT("Decal projection axes preserve non-square box dimensions"), Footprint.DecalSize.Equals(FVector(100.f, 80.f, 400.f)));
	Geometry.Shape.Type = ERPGHitQueryShape::LineSweep;
	Geometry.Shape.Length = 600.f;
	Geometry.Shape.Radius = 80.f;
	FRPGBossTelegraphFootprint::Build(Geometry, Footprint);
	TestTrue(TEXT("Sweep decal encloses both round caps"), Footprint.DecalSize.Equals(FVector(100.f, 80.f, 380.f)));
	TestTrue(TEXT("Sweep decal centered halfway along trajectory"), Footprint.Center.Equals(FVector(100.f, 550.f, 50.f), .01));
	Geometry.Shape.Type = ERPGHitQueryShape::RingSector;
	Geometry.Shape.InnerRadius = Geometry.Shape.Radius;
	TestFalse(TEXT("Invalid donut is refused"), FRPGBossTelegraphFootprint::Build(Geometry, Footprint));
	Geometry.Shape.Type = ERPGHitQueryShape::Sphere;
	Geometry.QueryTransform.SetRotation(FRotator(30.f, 0.f, 0.f).Quaternion());
	TestFalse(TEXT("Tilted ground footprint is refused"), FRPGBossTelegraphFootprint::Build(Geometry, Footprint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossTelegraphSnapshotShapeTest,
	"ProjectRPG.Boss.TelegraphShapes.AtomicSnapshotAndLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossTelegraphSnapshotShapeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); };
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Boss = World->SpawnActor<ARPGBossTestPawn>();
	if (!TestTrue(TEXT("Encounter starts"), Boss->Boss->StartEncounter())) return false;
	FRPGHitQueryShape Shape;
	Shape.Type = ERPGHitQueryShape::RingSector;
	Shape.Radius = 600.f;
	Shape.InnerRadius = 250.f;
	Shape.AngleDegrees = 360.f;
	const auto Tag = RPGGameplayTags::NPC_Ability_MeleeAttack.GetTag();
	const FTransform Transform(FRotator(0.f, 135.f, 0.f), FVector(100.f, 200.f, 0.f));
	const int32 First = Boss->Boss->BeginTelegraphWithShape(Tag, Transform, Shape, 10.f);
	TestTrue(TEXT("Shape windup accepted"), First > 0);
	const auto Frozen = Boss->Boss->GetTelegraphState();
	TestTrue(TEXT("Shape is present in the initial replicated snapshot"), Frozen.Geometry.bEnabled);
	TestEqual(TEXT("Inner radius captured exactly"), Frozen.Geometry.Shape.InnerRadius, 250.f);
	Shape.InnerRadius = 50.f;
	Boss->SetActorLocation(FVector(1000.f));
	TestEqual(TEXT("Authoring/source edits cannot move frozen geometry"), Frozen.Geometry.Shape.InnerRadius, 250.f);
	float Progress = 0.f;
	TestTrue(TEXT("Late observer can reconstruct valid shape"), URPGBossTelegraphPresentationComponent::EvaluateProgress(
		Boss->Boss->GetBossState(), Frozen, Frozen.StartServerTime + 5.f, Progress));
	TestEqual(TEXT("Late observer progress uses server clock"), Progress, .5f);
	TestEqual(TEXT("An active warning cannot be replaced"), Boss->Boss->BeginTelegraph(Tag, FVector::ZeroVector, 1.f), 0);
	TestTrue(TEXT("Explicit end succeeds"), Boss->Boss->EndTelegraph(Frozen.EncounterId, First));
	TestFalse(TEXT("Ended snapshot clears geometry"), Boss->Boss->GetTelegraphState().Geometry.bEnabled);
	const int32 Next = Boss->Boss->BeginTelegraph(Tag, FVector::ZeroVector, 5.f);
	TestTrue(TEXT("Legacy circle remains usable"), Next > First);
	TestFalse(TEXT("Legacy call cannot inherit old donut mask"), Boss->Boss->GetTelegraphState().Geometry.bEnabled);
	TestFalse(TEXT("Stale callback cannot end new warning"), Boss->Boss->EndTelegraph(Frozen.EncounterId, First));
	Boss->Boss->BeginReset();
	TestFalse(TEXT("Reset clears current warning"), Boss->Boss->GetTelegraphState().bActive);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossTelegraphDamageShapeTest,
	"ProjectRPG.Boss.TelegraphShapes.FrozenShapeDrivesActualDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGBossTelegraphDamageShapeTest::RunTest(const FString&)
{
	UWorld::InitializationValues Init;
	Init.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Damage world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { RPGTestWorld::Destroy(World); };
	World->InitializeActorsForPlay(FURL()); World->CreateAISystem(); World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	auto* Boss = World->SpawnActor<ARPGBossTestPawn>();
	auto* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	auto* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	const auto Spawn = [&](FVector Position)
	{
		auto* Target = World->SpawnActor<ARPGBossTestPawn>();
		auto* AI = World->SpawnActor<AAIController>();
		AI->SetGenericTeamId(FGenericTeamId(0)); AI->Possess(Target); Target->SetActorLocation(Position);
		return Target;
	};
	auto* Hole = Spawn(FVector(100.f, 0.f, 0.f));
	auto* Ring = Spawn(FVector(350.f, 0.f, 0.f));
	auto* Rear = Spawn(FVector(-350.f, 0.f, 0.f));
	Boss->Boss->StartEncounter();
	const int32 Encounter = Boss->Boss->GetBossState().EncounterId;
	Threat->AddThreat(Hole, 20.f, Encounter); Threat->AddThreat(Ring, 10.f, Encounter); Threat->AddThreat(Rear, 5.f, Encounter);
	World->Tick(LEVELTICK_All, .02f);
	const auto Handle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossTelegraphedStrikeAbility::StaticClass(), 1));
	auto* Attack = CastChecked<URPGBossTelegraphedStrikeAbility>(Boss->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Attack->ActionTag = RPGGameplayTags::NPC_Ability_MeleeAttack;
	Attack->WindupSeconds = .2f;
	Attack->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
	Attack->BaseDamage.Value = 31.f;
	Attack->bHitAllThreatParticipants = true;
	Attack->bUseCustomHitShape = true;
	Attack->bCenterHitShapeOnSource = true;
	Attack->HitShape.Type = ERPGHitQueryShape::RingSector;
	Attack->HitShape.Radius = 500.f; Attack->HitShape.InnerRadius = 200.f; Attack->HitShape.AngleDegrees = 90.f;
	if (!TestTrue(TEXT("Shaped strike activates"), Boss->ASC->TryActivateAbility(Handle, false))) return false;
	const auto Snapshot = Boss->Boss->GetTelegraphState();
	TestTrue(TEXT("Real attack publishes shape before its timer"), Snapshot.Geometry.bEnabled);
	TestEqual(TEXT("Real warning preserves the safe inner radius"), Snapshot.Geometry.Shape.InnerRadius, 200.f);
	// Editing the reusable ability after warning publication must not enlarge the active attack.
	Attack->bUseCustomHitShape = false; Attack->Radius = 1000.f;
	uint64 Frame = GFrameCounter;
	for (int32 I = 0; I < 30; ++I)
	{
		TGuardValue<uint64> Guard(GFrameCounter, ++Frame);
		World->Tick(LEVELTICK_All, .02f);
	}
	TestEqual(TEXT("Target in advertised safe center is unharmed"), Hole->Attributes->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Target in advertised ring receives actual GAS damage"), Ring->Attributes->GetCurrentHealth(), 69.f);
	TestEqual(TEXT("Target outside advertised sector is unharmed"), Rear->Attributes->GetCurrentHealth(), 100.f);
	TestFalse(TEXT("Impact ends its warning"), Boss->Boss->GetTelegraphState().bActive);
	return true;
}

#endif
