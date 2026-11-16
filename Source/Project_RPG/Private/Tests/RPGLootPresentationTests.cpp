#if WITH_DEV_AUTOMATION_TESTS
#include "Item/PickUp/RPGLootPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootArcTest, "ProjectRPG.Item.LootPresentation.Trajectory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLootArcTest::RunTest(const FString&)
{
	const FVector Start(20, 40, 120), End(180, -80, 5);
	using C = URPGLootPresentationComponent;
	TestEqual(TEXT("Starts at the corpse"), C::EvaluateArc(Start, End, 0, 100), Start);
	TestEqual(TEXT("Ends at the actual pickup anchor"), C::EvaluateArc(Start, End, 1, 100), End);
	TestEqual(TEXT("Arc midpoint clears the straight path"), C::EvaluateArc(Start, End, 0.5f, 100),
		(Start + End) * 0.5 + FVector(0, 0, 100));
	for (int32 Index = 0; Index <= 100; ++Index)
	{
		const float T = Index / 100.f;
		const FVector P = C::EvaluateArc(Start, End, T, 100);
		TestTrue(TEXT("Finite path"), !P.ContainsNaN());
		TestTrue(TEXT("Never dips below the start/end interpolation"), P.Z + 0.001 >= FMath::Lerp(Start.Z, End.Z, T));
	}
	TestEqual(TEXT("Late sampling clamps to rest"), C::EvaluateArc(Start, End, 4, 100), End);
	TestEqual(TEXT("Early sampling clamps to origin"), C::EvaluateArc(Start, End, -1, 100), Start);
	TestEqual(TEXT("Settle begins on ground"), C::EvaluateSettle(0), 0.f);
	TestEqual(TEXT("Settle ends without perpetual bobbing"), C::EvaluateSettle(1), 0.f);
	TestTrue(TEXT("Small secondary landing bounce"), C::EvaluateSettle(0.07f) > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootAuthorityLifecycleTest, "ProjectRPG.Item.LootPresentation.AuthorityAndReuse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLootAuthorityLifecycleTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("World"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Component = NewObject<URPGLootPresentationComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	const FGuid First = FGuid::NewGuid(), Next = FGuid::NewGuid();
	TestFalse(TEXT("Resting pickups do not fly"), Component->IsInFlight());
	TestFalse(TEXT("Missing logical drop rejected"), Component->BeginDropFlight({}, FVector::ZeroVector, 0));
	TestFalse(TEXT("Invalid origin rejected"), Component->BeginDropFlight(First,
		FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), 0));
	Owner->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot start a drop"), Component->BeginDropFlight(First, FVector::ZeroVector, 0));
	Owner->SetRole(ROLE_Authority);
	TestTrue(TEXT("Authority starts logical drop"), Component->BeginDropFlight(First, FVector(0, 0, 80), 2));
	TestTrue(TEXT("Cannot pick up during toss"), Component->IsInFlight());
	TestFalse(TEXT("Repeated server callback cannot restart the same drop"), Component->BeginDropFlight(First, FVector::ZeroVector, 4));
	Component->InitializeRestingDrop(First);
	TestTrue(TEXT("BeginPlay identity does not erase pre-spawn flight"), Component->IsInFlight());
	World->TimeSeconds = 5;
	TestFalse(TEXT("Server clock opens pickup after flight without a server visual tick"), Component->IsInFlight());
	TestTrue(TEXT("Can start a subsequent logical flight"), Component->BeginDropFlight(Next, FVector::ZeroVector, 0));
	Component->InitializeRestingDrop(Next);
	TestTrue(TEXT("Same drop identity preserves flight"), Component->IsInFlight());
	Component->InitializeRestingDrop(FGuid::NewGuid());
	TestFalse(TEXT("Pool reuse retires old trajectory"), Component->IsInFlight());
	Component->bEnabled = false;
	TestFalse(TEXT("Disabled presentation leaves immediate legacy interaction"), Component->BeginDropFlight(Next, FVector::ZeroVector, 0));
	TestFalse(TEXT("No unconditional tick"), Component->IsComponentTickEnabled());
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootAuthoredAssetsTest, "ProjectRPG.Item.LootPresentation.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLootAuthoredAssetsTest::RunTest(const FString&)
{
	TSet<FName> Variants;
	for (int32 Tier = 0; Tier < 5; ++Tier)
	{
		const FName Name = URPGLootPresentationComponent::DefaultEffectName(static_cast<ERPGLootRarity>(Tier));
		Variants.Add(Name);
		const FString Path = FString::Printf(TEXT("/Game/DrapEffet/VFX/%s.%s"), *Name.ToString(), *Name.ToString());
		TestNotNull(*Path, LoadObject<UNiagaraSystem>(nullptr, *Path));
	}
	TestEqual(TEXT("Five distinct authored rarity variants"), Variants.Num(), 5);
	for (const TCHAR* Cue : {TEXT("Pop_02_Cue"), TEXT("Abstract_Pop_03_Cue"), TEXT("Special_Musical_01_Cue"),
		TEXT("Special_Powerup_04_Cue"), TEXT("Special_Powerup_10_Cue")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Interface_And_Item_Sounds/Cues/%s.%s"), Cue, Cue);
		TestNotNull(*Path, LoadObject<USoundBase>(nullptr, *Path));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGLootVisualLifecycleTest, "ProjectRPG.Item.LootPresentation.VisualLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGLootVisualLifecycleTest::RunTest(const FString&)
{
	// Preload authored data so this test observes production components without IO timing assumptions.
	LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/DrapEffet/VFX/NE_drop_effect05.NE_drop_effect05"));
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Visual fixture world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Root = NewObject<UStaticMeshComponent>(Owner);
	Owner->AddInstanceComponent(Root);
	Owner->SetRootComponent(Root);
	Root->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Root->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Root->SetWorldScale3D(FVector(2));
	Root->RegisterComponent();
	Owner->SetActorLocation(FVector(150, 0, 5));
	auto* Component = NewObject<URPGLootPresentationComponent>(Owner);
	Owner->AddInstanceComponent(Component);
	Component->RegisterComponent();
	Component->BeginDropFlight(FGuid::NewGuid(), FVector(0, 0, 100), 0);
	Owner->DispatchBeginPlay();
	UStaticMeshComponent* Proxy = nullptr;
	for (auto* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Owner))
		if (Mesh != Root) Proxy = Cast<UStaticMeshComponent>(Mesh);
	if (TestNotNull(TEXT("One presentation mesh"), Proxy))
	{
		TestFalse(TEXT("Original draw is hidden without removing its collision"), Root->IsVisible());
		TestEqual(TEXT("Collision shape is preserved"), Root->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		TestEqual(TEXT("Proxy cannot grant or collide"), Proxy->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		Component->SetFocused(true);
		TestTrue(TEXT("Interaction outline follows visible proxy"), Proxy->bRenderCustomDepth);
		Component->SetFocused(false);
		TestFalse(TEXT("Interaction outline clears"), Proxy->bRenderCustomDepth);
		World->TimeSeconds = 0.35;
		Component->TickComponent(0.35f, LEVELTICK_All, nullptr);
		TestEqual(TEXT("Logical anchor stays fixed during flight"), Owner->GetActorLocation(), FVector(150, 0, 5));
		TestTrue(TEXT("Visual mesh clears the landing point"), Proxy->GetComponentLocation().Z > 100);
		TestEqual(TEXT("Ground beam waits for landing"), TInlineComponentArray<UNiagaraComponent*>(Owner).Num(), 0);
		World->TimeSeconds = 1;
		Component->TickComponent(0.65f, LEVELTICK_All, nullptr);
		const TInlineComponentArray<UNiagaraComponent*> Beams(Owner);
		TestEqual(TEXT("Exactly one grounded effect after landing"), Beams.Num(), 1);
		if (Beams.Num() == 1)
		{
			auto* Beam = CastChecked<UNiagaraComponent>(Beams[0]);
			TestTrue(TEXT("Beam scale ignores authored item scale"), Beam->GetComponentScale().Equals(FVector(0.6f)));
			Component->SetPickupAvailable(false);
			TestFalse(TEXT("Pending claim hides its beam"), Beam->IsVisible());
			Component->SetPickupAvailable(true);
			TestTrue(TEXT("Rejected claim restores beam"), Beam->IsVisible());
		}
		TestFalse(TEXT("Resting items do not tick"), Component->IsComponentTickEnabled());
		Component->NotifyCollected();
		TestFalse(TEXT("Confirmed collection hides proxy"), Proxy->IsVisible());
		Component->InitializeRestingDrop(FGuid::NewGuid());
		TestTrue(TEXT("Reused pickup has a visible proxy"), Proxy->IsVisible());
		TestEqual(TEXT("Pool reuse has one fresh beam"), TInlineComponentArray<UNiagaraComponent*>(Owner).Num(), 1);
	}
	Owner->Destroy();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
