#include "Tests/RPGRidingLocalInteractionTests.h"
#include "Tests/RPGRidingActorTests.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "Riding/RPGRidingLocalInteractionComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"

ARPGRidingLocalTarget::ARPGRidingLocalTarget()
{ SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root"))); bReplicates = true; }
bool ARPGRidingLocalTarget::CanApproveRidingLocalInteraction(APlayerController* Controller) const
{
    return bAllowed && IsValid(Controller) && IsValid(Controller->GetPawn())
        && FVector::DistSquared(Controller->GetPawn()->GetActorLocation(), GetActorLocation()) <= 10000;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingLocalInteractionTestAccess
{
    static bool Prepare(URPGRidingLocalInteractionComponent& C, AActor& T, TFunction<void()> Continue) { return C.Prepare(T, MoveTemp(Continue)); }
    static uint32 Sequence(const URPGRidingLocalInteractionComponent& C) { return C.LocalSequence; }
    static void Reply(URPGRidingLocalInteractionComponent& C, uint32 Seq, const FRPGRidingStateKey& Key, bool Accepted = true) { C.ClientPrepared_Implementation(Seq, Accepted, Key); }
    static void Server(URPGRidingLocalInteractionComponent& C, uint32 Seq, APawn* P, AActor* T, FGuid Id, const FRPGRidingStateKey& Key) { C.ServerPrepare_Implementation(Seq, P, T, Id, Key); }
    static void Poll(URPGRidingLocalInteractionComponent& C) { C.Poll(); }
    static void Expire(URPGRidingLocalInteractionComponent& C) { C.Deadline = FPlatformTime::Seconds() - 1; C.Poll(); }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* PC;
        ARPGRidingTestPawn* Pawn;
        ARPGRidingLocalTarget* Target;
        URPGRidingComponent* Riding;
        URPGRidingLocalInteractionComponent* Gate;
        int32 Executions = 0;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
                .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
            PC = World->SpawnActor<APlayerController>(); auto* PS = World->SpawnActor<APlayerState>();
            PC->SetAsLocalPlayerController(); PC->SetPlayerState(PS); PS->SetOwner(PC);
            Pawn = World->SpawnActor<ARPGRidingTestPawn>(); PC->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            auto* Session = NewObject<URPGRidingSessionComponent>(PS); Session->RegisterComponent();
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>(); RPGPrepareRidingMovementTestSession(*Session, *Riding);
            Riding->RequestSetMounted(true, {});
            Target = World->SpawnActor<ARPGRidingLocalTarget>(); Target->SetActorLocation(Pawn->GetActorLocation());
            Gate = NewObject<URPGRidingLocalInteractionComponent>(PC); PC->AddInstanceComponent(Gate); Gate->RegisterComponent();
        }
        ~FFixture()
        {
            // This standalone fixture temporarily impersonates an owning client.
            // Restore the real actor role before engine teardown changes replication.
            if (IsValid(Pawn)) Pawn->SetRole(ROLE_Authority);
            RPGTestWorld::Destroy(World);
        }
        bool Request() { return Gate->Request(*Target, [&] { ++Executions; }); }
        bool PrepareClient()
        {
            Pawn->SetRole(ROLE_AutonomousProxy);
            return FRPGRidingLocalInteractionTestAccess::Prepare(*Gate, *Target, [&] { ++Executions; });
        }
        FRPGRidingRepState Foot() const
        { FRPGRidingRepState State; State.Key = Riding->GetRidingState().Key; ++State.Key.StateRevision; State.LastEndReason = ERPGRidingEndReason::Manual; return State; }
        void State(const FRPGRidingRepState& Value) { RPGSetRidingVisualTestState(*Riding, Value, {}); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalApproval, "ProjectRPG.Riding.LocalInteraction.Approval", Flags)
bool FRidingLocalApproval::RunTest(const FString&)
{
    FFixture F; TestTrue(TEXT("Mounted source"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    TestTrue(TEXT("Native owned server/client RPC path accepts current target"), F.Request());
    TestTrue(TEXT("Server dismounted original Pawn before single local continuation"), F.Executions == 1 && !F.Gate->IsPending()
        && F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot && F.PC->GetPawn() == F.Pawn);
    TestFalse(TEXT("Idle screen requires no further handshake"), URPGRidingLocalInteractionComponent::NeedsApproval(F.PC));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalRejection, "ProjectRPG.Riding.LocalInteraction.RejectionAndReplay", Flags)
bool FRidingLocalRejection::RunTest(const FString&)
{
    {
        FFixture F; F.Target->bAllowed = false;
        TestFalse(TEXT("Local target preflight rejects without dismount"), F.Request());
        F.Target->bAllowed = true; F.Pawn->Attributes->CurrentHealth.SetCurrentValue(0);
        F.Request();
        TestTrue(TEXT("Server health rejection preserves mount and invokes no UI"), F.Executions == 0 && !F.Gate->IsPending()
            && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    }
    {
        FFixture F;
        const auto Handle = F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& S)
            { if (S.Mode == ERPGRidingMode::OnFoot) F.Target->Identity = FGuid::NewGuid(); });
        F.Request(); F.Riding->OnChanged.Remove(Handle);
        TestTrue(TEXT("Server rechecks target identity after dismount callbacks"), F.Executions == 0 && !F.Gate->IsPending());
    }
    {
        FFixture F; const auto Key = F.Riding->GetRidingState().Key;
        F.Request(); const auto Seq = FRPGRidingLocalInteractionTestAccess::Sequence(*F.Gate);
        F.Riding->RequestSetMounted(true, {});
        FRPGRidingLocalInteractionTestAccess::Server(*F.Gate, Seq, F.Pawn, F.Target, F.Target->Identity, Key);
        TestTrue(TEXT("Duplicate request cannot dismount a later mount or replay UI"), F.Executions == 1
            && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalOrdering, "ProjectRPG.Riding.LocalInteraction.ClientOrdering", Flags)
bool FRidingLocalOrdering::RunTest(const FString&)
{
    for (bool StateFirst : {false, true})
    {
        FFixture F; const auto Foot = F.Foot(); if (!TestTrue(TEXT("Pending owner request"), F.PrepareClient())) return false;
        const auto Seq = FRPGRidingLocalInteractionTestAccess::Sequence(*F.Gate);
        if (StateFirst) F.State(Foot);
        FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, Seq, Foot.Key);
        if (!StateFirst)
        {
            TestTrue(TEXT("Receipt alone cannot write state or open screen"), F.Gate->IsPending() && F.Executions == 0
                && F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
            F.State(Foot); FRPGRidingLocalInteractionTestAccess::Poll(*F.Gate);
        }
        FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, Seq, Foot.Key);
        TestTrue(TEXT("Either delivery order completes exactly once"), F.Executions == 1 && !F.Gate->IsPending());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalCancellation, "ProjectRPG.Riding.LocalInteraction.Cancellation", Flags)
bool FRidingLocalCancellation::RunTest(const FString&)
{
    FFixture F; const auto Foot = F.Foot(); F.PrepareClient();
    const auto Old = FRPGRidingLocalInteractionTestAccess::Sequence(*F.Gate); F.Gate->Cancel(); F.PrepareClient();
    const auto Fresh = FRPGRidingLocalInteractionTestAccess::Sequence(*F.Gate);
    FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, Old, Foot.Key);
    TestTrue(TEXT("Late reply cannot replace a newer request"), F.Gate->IsPending() && F.Executions == 0 && Fresh > Old);
    FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, Fresh, Foot.Key); F.Gate->Cancel(); F.State(Foot);
    FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, Fresh, Foot.Key); FRPGRidingLocalInteractionTestAccess::Poll(*F.Gate);
    TestTrue(TEXT("Released input cancels even after approval but before replication"), !F.Gate->IsPending() && F.Executions == 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalLifetime, "ProjectRPG.Riding.LocalInteraction.Lifetime", Flags)
bool FRidingLocalLifetime::RunTest(const FString&)
{
    for (int32 Case = 0; Case < 6; ++Case)
    {
        FFixture F; const auto Foot = F.Foot(); F.PrepareClient();
        switch (Case)
        {
        case 0: F.Target->Destroy(); break;
        case 1: F.Target->Identity = FGuid::NewGuid(); break;
        case 2: F.PC->SetPawn(nullptr); break; // Owning-client Pawn loss, not an authority-only UnPossess call.
        case 3: FRPGRidingLocalInteractionTestAccess::Expire(*F.Gate); break;
        case 4: F.Gate->UnregisterComponent(); break;
        default:
            auto NewMount = F.Riding->GetRidingState(); NewMount.Key.StateRevision += 2; F.State(NewMount); break;
        }
        FRPGRidingLocalInteractionTestAccess::Reply(*F.Gate, FRPGRidingLocalInteractionTestAccess::Sequence(*F.Gate), Foot.Key);
        FRPGRidingLocalInteractionTestAccess::Poll(*F.Gate);
        TestTrue(TEXT("Target/config/Pawn/timeout/component/new mount loss discards continuation"), F.Executions == 0 && !F.Gate->IsPending());
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingLocalWire, "ProjectRPG.Riding.LocalInteraction.Wire", Flags)
bool FRidingLocalWire::RunTest(const FString&)
{
    const auto* Server = URPGRidingLocalInteractionComponent::StaticClass()->FindFunctionByName(TEXT("ServerPrepare"));
    const auto* Client = URPGRidingLocalInteractionComponent::StaticClass()->FindFunctionByName(TEXT("ClientPrepared"));
    TestTrue(TEXT("Request is a reflected reliable server RPC"), Server && Server->HasAllFunctionFlags(FUNC_Net | FUNC_NetReliable | FUNC_NetServer));
    TestTrue(TEXT("Receipt is a reflected reliable owning-client RPC"), Client && Client->HasAllFunctionFlags(FUNC_Net | FUNC_NetReliable | FUNC_NetClient));
    TestTrue(TEXT("Request epoch/revision is reflected"), Server && FindFProperty<FStructProperty>(Server, TEXT("InExpected")));
    TestTrue(TEXT("Receipt key is reflected"), Client && FindFProperty<FStructProperty>(Client, TEXT("InApproved")));
    TestTrue(TEXT("Stable target fingerprint is deterministic and configuration dependent"),
        IInteractionInterface::MakeRidingLocalInteractionIdentity(TEXT("a|b")) == IInteractionInterface::MakeRidingLocalInteractionIdentity(TEXT("a|b"))
        && IInteractionInterface::MakeRidingLocalInteractionIdentity(TEXT("a|b")) != IInteractionInterface::MakeRidingLocalInteractionIdentity(TEXT("a|c")));
    TestTrue(TEXT("Non-ASCII asset paths retain distinct identities"),
        IInteractionInterface::MakeRidingLocalInteractionIdentity(FString::Chr(0xAC00))
        != IInteractionInterface::MakeRidingLocalInteractionIdentity(FString::Chr(0xAC01)));
    return true;
}
#endif
