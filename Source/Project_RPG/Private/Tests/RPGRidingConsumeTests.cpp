#include "Riding/RPGRidingConsumeLease.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Tests/RPGRidingActorTests.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingTestPawn* Pawn;
        URPGRidingComponent* Riding;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Pawn = Spawn(); PC->Possess(Pawn);
            auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
        }
        ARPGRidingTestPawn* Spawn()
        {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Result = World->SpawnActor<ARPGRidingTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
            Result->ASC->InitAbilityActorInfo(Result, Result); Result->ASC->AddAttributeSetSubobject(Result->Attributes.Get());
            Result->Attributes->CurrentHealth.SetBaseValue(100); Result->Attributes->CurrentHealth.SetCurrentValue(100);
            Result->GetCharacterMovement()->MovementMode = MOVE_Walking;
            return Result;
        }
        ~FFixture() { World->DestroyWorld(false); }
        bool Mount() { Riding->RequestSetMounted(true, {}); return Riding->GetRidingState().Mode == ERPGRidingMode::Mounted; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingConsumeMutualTest, "ProjectRPG.Riding.Consume.MutualExclusion", Flags)
bool FRidingConsumeMutualTest::RunTest(const FString&)
{
    FFixture F;
    if (!TestTrue(TEXT("Fixture mount approved"), F.Mount())) return false;
    TestFalse(TEXT("Mounted pawn cannot prepare consumption"), FRPGRidingConsumeLease::TryAcquire(*F.Pawn).IsValid());
    TestFalse(TEXT("Rejected preparation leaves no reservation"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    F.Riding->RequestSetMounted(false, {});
    auto Lease = FRPGRidingConsumeLease::TryAcquire(*F.Pawn);
    if (!TestTrue(TEXT("On-foot preparation permitted"), Lease.IsValid())) return false;
    TestTrue(TEXT("Prepared request visible to server guard"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    TestFalse(TEXT("Actual server mount is rejected during preparation"), F.Mount());
    Lease.Reset();
    TestFalse(TEXT("Definitive discard releases preparation"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    TestTrue(TEXT("Fresh mount succeeds after discard"), F.Mount());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingConsumePinTest, "ProjectRPG.Riding.Consume.PendingAndCallbackPin", Flags)
bool FRidingConsumePinTest::RunTest(const FString&)
{
    FFixture F;
    TMap<FGuid, TSharedPtr<FRPGRidingConsumeLease>> PreparedTargets;
    const FGuid Request = FGuid::NewGuid(); PreparedTargets.Add(Request, FRPGRidingConsumeLease::TryAcquire(*F.Pawn));
    TestTrue(TEXT("Uncertain request retains original target reservation"), PreparedTargets[Request].IsValid());
    TestFalse(TEXT("Uncertain original request blocks mount"), F.Mount());
    const auto PinnedTarget = PreparedTargets.FindRef(Request);
    PreparedTargets.Remove(Request); // The real consume sink also pins its original target across GE callbacks.
    TestTrue(TEXT("Removing map entry does not release a pinned target"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    TestFalse(TEXT("Reentrant mount cannot overtake effect delivery"), F.Mount());
    TestTrue(TEXT("Pin remains live after callback"), PinnedTarget.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingConsumeIndependentTest, "ProjectRPG.Riding.Consume.IndependentReservations", Flags)
bool FRidingConsumeIndependentTest::RunTest(const FString&)
{
    FFixture F;
    auto First = FRPGRidingConsumeLease::TryAcquire(*F.Pawn), Second = FRPGRidingConsumeLease::TryAcquire(*F.Pawn);
    First.Reset();
    TestTrue(TEXT("One release cannot release another request"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    TestFalse(TEXT("Another prepared request still blocks mounting"), F.Mount());
    Second.Reset();
    TestTrue(TEXT("All requests finished permits mount"), F.Mount());
    F.Riding->RequestSetMounted(false, {});
    {
        const auto FailedPreparation = FRPGRidingConsumeLease::TryAcquire(*F.Pawn);
        TestTrue(TEXT("Preparation can fail before storage without losing its guard"), FailedPreparation.IsValid());
    }
    TestFalse(TEXT("Early return releases local preparation scope"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    F.Pawn->SetRole(ROLE_AutonomousProxy);
    TestFalse(TEXT("Client cannot acquire authoritative consume permission"), FRPGRidingConsumeLease::TryAcquire(*F.Pawn).IsValid());
    F.Pawn->SetRole(ROLE_Authority);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingConsumeLifetimeTest, "ProjectRPG.Riding.Consume.PawnAndBounds", Flags)
bool FRidingConsumeLifetimeTest::RunTest(const FString&)
{
    FFixture F;
    TArray<TSharedPtr<FRPGRidingConsumeLease>> Leases;
    for (int32 Index = 0; Index < 64; ++Index) Leases.Add(FRPGRidingConsumeLease::TryAcquire(*F.Pawn));
    TestTrue(TEXT("Bounded prepared requests accepted"), Leases.Last().IsValid());
    TestFalse(TEXT("Excess native preparations fail closed"), FRPGRidingConsumeLease::TryAcquire(*F.Pawn).IsValid());
    auto* Replacement = F.Spawn();
    TestFalse(TEXT("Another Pawn never inherits old reservations"), FRPGRidingConsumeLease::HasPending(Replacement));
    auto OtherLease = FRPGRidingConsumeLease::TryAcquire(*Replacement);
    TestTrue(TEXT("Different Pawn has independent reservation"), OtherLease.IsValid());
    Leases.Reset();
    TestFalse(TEXT("Old Pawn fully released"), FRPGRidingConsumeLease::HasPending(F.Pawn));
    TestTrue(TEXT("Old release does not free replacement's reservation"), FRPGRidingConsumeLease::HasPending(Replacement));
    OtherLease.Reset();
    auto* BarePawn = F.World->SpawnActor<APawn>();
    const auto BareLease = FRPGRidingConsumeLease::TryAcquire(*BarePawn);
    TestTrue(TEXT("Pawn without Riding component retains existing consume support"), BareLease.IsValid());
    return true;
}
#endif
