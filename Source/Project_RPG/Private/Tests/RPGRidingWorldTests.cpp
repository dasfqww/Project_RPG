#include "Riding/RPGRidingWorldPolicy.h"
#include "Riding/RPGRidingRegionVolume.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Tests/RPGRidingActorTests.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FWorld
    {
        UWorld* World;
        ARPGRidingTestPawn* Pawn;
        URPGRidingWorldPolicy* Policy;
        FWorld()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            Pawn = World->SpawnActor<ARPGRidingTestPawn>();
            Policy = World->GetSubsystem<URPGRidingWorldPolicy>();
        }
        ~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
        ARPGRidingRegionVolume* Allow(FVector Center = FVector::ZeroVector, FVector Size = FVector(1000))
        {
            auto* A = World->SpawnActor<ARPGRidingRegionVolume>(); A->SetActorLocation(Center); A->Bounds->SetBoxExtent(Size); return A;
        }
        ARPGNoRidingVolume* Deny(FVector Center, FVector Size)
        {
            auto* A = World->SpawnActor<ARPGNoRidingVolume>(); A->SetActorLocation(Center); A->Bounds->SetBoxExtent(Size); return A;
        }
        FRPGRidingPathPermission Query(FVector Start = FVector::ZeroVector, FVector End = FVector::ZeroVector,
            const FGameplayTagContainer& Tags = {}) const { return Policy->EvaluatePath(*Pawn, Start, End, Tags); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingWorldDefaultTest, "ProjectRPG.Riding.World.DefaultAndLifecycle", Flags)
bool FRidingWorldDefaultTest::RunTest(const FString&)
{
    FWorld F;
    if (!TestNotNull(TEXT("Game world has policy"), F.Policy)) return false;
    TestFalse(TEXT("Unauthored map defaults to Deny"), F.Query().bAllowed);
    auto* A = F.Allow();
    TestTrue(TEXT("Explicit Allow contains whole capsule"), F.Query().bAllowed);
    TestFalse(TEXT("Default policy never changes capsule collision"), A->Bounds->IsCollisionEnabled());
    A->SetActorLocation(FVector(5000, 0, 0));
    TestFalse(TEXT("Moving authoring region is re-evaluated"), F.Query().bAllowed);
    A->SetActorLocation(FVector::ZeroVector);
    auto* D = F.Deny(FVector::ZeroVector, FVector(50));
    TestFalse(TEXT("Any Deny wins"), F.Query().bAllowed);
    D->Destroy();
    TestTrue(TEXT("Unloaded/destroyed region grants no stale Deny"), F.Query().bAllowed);
    A->Bounds->SetBoxExtent(FVector::ZeroVector);
    TestFalse(TEXT("Invalid policy geometry fails closed"), F.Query().bAllowed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingWorldPathTest, "ProjectRPG.Riding.World.SweptPath", Flags)
bool FRidingWorldPathTest::RunTest(const FString&)
{
    FWorld F; F.Allow(FVector::ZeroVector, FVector(10000));
    auto* D = F.Deny(FVector(500, 0, 0), FVector(1, 100, 500));
    const FVector End(1000, 0, 0);
    TestTrue(TEXT("End alone is allowed"), F.Query(End, End).bAllowed);
    const auto Path = F.Query(FVector::ZeroVector, End);
    const double Expected = (499. - F.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius()) / 1000.;
    TestFalse(TEXT("Thin forbidden region cannot be skipped at high speed"), Path.bAllowed);
    TestTrue(TEXT("First capsule contact fraction"), FMath::IsNearlyEqual(Path.SafeFraction, Expected, 0.00001));
    D->SetActorRotation(FRotator(0, 45, 0));
    TestFalse(TEXT("Rotated deny geometry is swept"), F.Query(FVector::ZeroVector, End).bAllowed);
    D->Destroy();
    TestTrue(TEXT("No fabricated physical obstacle"), F.Query(FVector::ZeroVector, End).bAllowed);
    const auto Outside = F.Query(FVector::ZeroVector, FVector(20000, 0, 0));
    TestTrue(TEXT("Leaving Allow clips before unauthored space"), !Outside.bAllowed && Outside.SafeFraction > 0 && Outside.SafeFraction < 1);
    TestFalse(TEXT("Capsule cannot fit in low Allow ceiling"), F.Allow(FVector(0, 20000, 0), FVector(500, 500, 10)) && F.Query(FVector(0, 20000, 0), FVector(0, 20000, 0)).bAllowed);
    FVector Bad = End; Bad.X = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Nonfinite movement is denied"), F.Query(FVector::ZeroVector, Bad).bAllowed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingWorldTagsTest, "ProjectRPG.Riding.World.RestrictionIntersection", Flags)
bool FRidingWorldTagsTest::RunTest(const FString&)
{
    FWorld F;
    const auto A = FGameplayTag::RequestGameplayTag(TEXT("Riding.Test.A"));
    const auto B = FGameplayTag::RequestGameplayTag(TEXT("Riding.Test.B"));
    auto* First = F.Allow(); First->AllowMountTags.AddTag(A); First->AllowMountTags.AddTag(B);
    auto* Second = F.Allow(); Second->AllowMountTags.AddTag(B);
    TestTrue(TEXT("Exact common mount tag permitted"), F.Query({}, {}, FGameplayTagContainer(B)).bAllowed);
    TestFalse(TEXT("Matching only one Allow is insufficient"), F.Query({}, {}, FGameplayTagContainer(A)).bAllowed);
    Second->AllowMountTags.Reset(); Second->AllowMountTags.AddTag(A);
    First->AllowMountTags.Reset(); First->AllowMountTags.AddTag(B);
    FGameplayTagContainer Both; Both.AddTag(A); Both.AddTag(B);
    TestFalse(TEXT("Disjoint intersection denied even if mount has both tags"), F.Query({}, {}, Both).bAllowed);
    First->Destroy(); Second->Destroy(); F.Allow();
    auto* Small = F.Allow(FVector(100, 0, 0), FVector(50)); Small->AllowMountTags.AddTag(A);
    TestFalse(TEXT("Partial capsule contact cannot evade restriction inside larger Allow"), F.Query(FVector(60, 0, 0), FVector(60, 0, 0)).bAllowed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingWorldActorTest, "ProjectRPG.Riding.World.AuthorityTransitions", Flags)
bool FRidingWorldActorTest::RunTest(const FString&)
{
    FWorld F;
    auto* PC = F.World->SpawnActor<APlayerController>(); auto* PS = F.World->SpawnActor<APlayerState>();
    PC->SetPlayerState(PS); PS->SetOwner(PC); PC->Possess(F.Pawn);
    auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
    F.Pawn->ASC->InitAbilityActorInfo(F.Pawn, F.Pawn); F.Pawn->ASC->AddAttributeSetSubobject(F.Pawn->Attributes.Get());
    F.Pawn->Attributes->CurrentHealth.SetBaseValue(100); F.Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
    F.Pawn->GetCharacterMovement()->MovementMode = MOVE_Walking;
    auto* Riding = F.Pawn->FindComponentByClass<URPGRidingComponent>();
    RPGPrepareRidingMovementTestSession(*Session, *Riding);
    uint32 Sequence = 0;
    const auto Mount = [&]() { FRPGRidingSetCommand C; C.RiderEpoch = Riding->GetRidingState().Key.RiderEpoch;
        C.ExpectedStateRevision = Riding->GetRidingState().Key.StateRevision; C.RequestSeq = ++Sequence; C.bDesiredMounted = true;
        RPGSendRidingMovementTestCommand(*Riding, C); };
    auto* D = F.Deny(FVector::ZeroVector, FVector(50)); Mount();
    TestTrue(TEXT("Real server command denied in forbidden area"), Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    D->Destroy(); Mount();
    if (!TestTrue(TEXT("Command approved in authored region"), Riding->GetRidingState().Mode == ERPGRidingMode::Mounted)) return false;
    D = F.Deny(FVector(500, 0, 0), FVector(1, 100, 500));
    const auto Key = Riding->GetRidingState().Key;
    const double Fraction = Riding->ConstrainServerMovement(F.Pawn->GetActorLocation(), FVector(1000, 0, 0));
    TestTrue(TEXT("Server movement clips at first region boundary"), Fraction > 0 && Fraction < 1);
    TestTrue(TEXT("Boundary publishes authoritative Region dismount"), Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot
        && Riding->GetRidingState().LastEndReason == ERPGRidingEndReason::Region && Riding->GetRidingState().Key.StateRevision > Key.StateRevision);
    TestEqual(TEXT("Region policy never teleports the Pawn"), F.Pawn->GetActorLocation(), FVector::ZeroVector);
    D->Destroy(); Mount();
    D = F.Deny(FVector::ZeroVector, FVector(50)); Riding->ValidateServerMovementContext();
    TestTrue(TEXT("Region change/base displacement reevaluation ends mount"), Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    D->Destroy(); Mount();
    D = F.Deny(FVector(500, 0, 0), FVector(1, 100, 500));
    const auto Callback = Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    { if (State.Mode == ERPGRidingMode::OnFoot) F.Pawn->SetActorLocation(FVector(2000, 0, 0)); });
    TestEqual(TEXT("Dismount callback relocation invalidates old movement fraction"),
        Riding->ConstrainServerMovement(FVector::ZeroVector, FVector(1000, 0, 0)), 0.);
    Riding->OnChanged.Remove(Callback);
    TestEqual(TEXT("Policy preserves callback-owned relocation"), F.Pawn->GetActorLocation(), FVector(2000, 0, 0));
    return true;
}
#endif
