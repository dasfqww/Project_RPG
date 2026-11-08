#include "Tests/RPGRidingDamageTests.h"
#include "Tests/RPGRidingActorTests.h"
#include "Riding/RPGRidingDamageScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGMountDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FPlayer
    {
        ARPGRidingDamageTestController* PC = nullptr;
        ARPGRidingTestPawn* Pawn = nullptr;
        URPGRidingSessionComponent* Session = nullptr;
        URPGRidingComponent* Riding() const { return Pawn->FindComponentByClass<URPGRidingComponent>(); }
        bool Mount() const { Riding()->RequestSetMounted(true, {}); return Riding()->GetRidingState().Mode == ERPGRidingMode::Mounted; }
    };
    struct FFixture
    {
        UWorld* World;
        FPlayer A, B;
        FFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            A = Player(1); B = Player(2);
        }
        ARPGRidingTestPawn* Spawn()
        {
            FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Pawn = World->SpawnActor<ARPGRidingTestPawn>(FVector::ZeroVector, FRotator::ZeroRotator, P);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->CurrentHealth.SetBaseValue(100); Pawn->Attributes->CurrentHealth.SetCurrentValue(100);
            Pawn->GetCharacterMovement()->MovementMode = MOVE_Walking;
            return Pawn;
        }
        FPlayer Player(uint8 Team)
        {
            FPlayer P; P.PC = World->SpawnActor<ARPGRidingDamageTestController>(); P.PC->Team = FGenericTeamId(Team);
            auto* PS = World->SpawnActor<APlayerState>(); P.PC->SetAsLocalPlayerController(); P.PC->SetPlayerState(PS); PS->SetOwner(P.PC);
            P.Pawn = Spawn(); P.PC->Possess(P.Pawn);
            P.Session = NewObject<URPGRidingSessionComponent>(PS); P.Session->RegisterComponent();
            RPGPrepareRidingMovementTestSession(*P.Session, *P.Riding());
            return P;
        }
        ~FFixture() { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDamageOutcomeTest, "ProjectRPG.Riding.Damage.HealthAndShield", Flags)
bool FRidingDamageOutcomeTest::RunTest(const FString&)
{
    for (const bool ShieldOnly : {false, true})
    {
        FFixture F;
        if (!TestTrue(TEXT("Both fixtures mounted"), F.A.Mount() && F.B.Mount())) return false;
        bool BothLockedAtFirstCallback = false;
        ERPGRidingResultCode OtherResult = ERPGRidingResultCode::NoChange;
        F.B.Riding()->OnResult.AddLambda([&](const FRPGRidingCommandResult& R) { OtherResult = R.Result; });
        F.A.Riding()->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
        {
            if (State.Mode == ERPGRidingMode::OnFoot)
            {
                F.B.Riding()->RequestSetMounted(true, {});
                BothLockedAtFirstCallback = F.A.Session->GetOwnerState().CombatEndServerSeconds > 9.f
                    && OtherResult == ERPGRidingResultCode::ActionBlocked
                    && F.B.Session->GetOwnerState().CombatEndServerSeconds > 9.f;
            }
        });
        {
            FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC);
            Scope.Complete(ShieldOnly ? 0.f : 7.f, ShieldOnly ? 7.f : 0.f);
            const double End = F.A.Session->GetOwnerState().CombatEndServerSeconds;
            Scope.Complete(50.f, 0.f);
            TestEqual(TEXT("Scope cannot replay/extend outcome"), F.A.Session->GetOwnerState().CombatEndServerSeconds, End);
        }
        TestTrue(TEXT("Outgoing residual hit dismounts attacker"), F.A.Riding()->GetRidingState().LastEndReason == ERPGRidingEndReason::Hit);
        TestTrue(TEXT("HP or fully absorbed damage dismounts victim"), F.B.Riding()->GetRidingState().LastEndReason == ERPGRidingEndReason::Hit);
        TestTrue(TEXT("First callback cannot replace other mounted participant"), BothLockedAtFirstCallback);
        TestTrue(TEXT("Both ten-second deadlines published"), F.A.Session->GetOwnerState().CombatEndServerSeconds >= 9.9
            && F.B.Session->GetOwnerState().CombatEndServerSeconds >= 9.9);
        TestFalse(TEXT("After scope release combat lock still rejects mount"), F.A.Mount());
        F.A.Riding()->OnChanged.Clear();
        F.B.Riding()->OnResult.Clear();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDamageNoOutcomeTest, "ProjectRPG.Riding.Damage.RejectedAndNested", Flags)
bool FRidingDamageNoOutcomeTest::RunTest(const FString&)
{
    FFixture F;
    {
        FRPGRidingDamageScope Outer(F.A.Pawn->ASC, F.B.Pawn->ASC);
        {
            FRPGRidingDamageScope Inner(F.A.Pawn->ASC, F.B.Pawn->ASC);
            Inner.Complete(0.f, 0.f);
        }
        TestFalse(TEXT("Inner release cannot clear outer admission barrier"), F.A.Mount());
        Outer.Complete(-1.f, 0.f);
    }
    TestTrue(TEXT("Aborted/rejected scopes release without combat lock"), F.A.Mount());
    const auto Revision = F.A.Riding()->GetRidingState().Key.StateRevision;
    {
        FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC);
        F.A.Riding()->ValidateServerMovementContext();
        FRPGRidingMovementProfile Profile;
        TestTrue(TEXT("Pending outcome preserves existing mounted movement"), F.A.Riding()->TryBuildMovementProfile(Profile));
        Scope.Complete(0.f, 0.f);
    }
    TestEqual(TEXT("Zero result does not dismount"), F.A.Riding()->GetRidingState().Key.StateRevision, Revision);
    TestTrue(TEXT("Zero result does not record combat"), F.A.Session->GetOwnerState().CombatEndServerSeconds <= 0.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDamageAuthorityTest, "ProjectRPG.Riding.Damage.AuthorityAndTeams", Flags)
bool FRidingDamageAuthorityTest::RunTest(const FString&)
{
    FFixture F;
    F.B.PC->Team = F.A.PC->Team;
    { FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC); Scope.Complete(5.f, 0.f); }
    TestTrue(TEXT("Friendly damage cannot create hostile lock"), F.A.Mount() && F.B.Mount());
    F.B.PC->Team = FGenericTeamId(2); F.B.Pawn->SetRole(ROLE_AutonomousProxy);
    { FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC); Scope.Complete(5.f, 0.f); }
    F.B.Pawn->SetRole(ROLE_Authority);
    TestTrue(TEXT("Client-side result cannot force dismount"), F.A.Riding()->GetRidingState().Mode == ERPGRidingMode::Mounted);
    { FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.A.Pawn->ASC); Scope.Complete(5.f, 0.f); }
    TestTrue(TEXT("Self damage is not hostile combat"), F.A.Session->GetOwnerState().CombatEndServerSeconds <= 0.01);
    F.B.Pawn->ASC->ClearActorInfo();
    { FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC); Scope.Complete(5.f, 0.f); }
    TestTrue(TEXT("Detached ASC cannot author combat"), F.A.Session->GetOwnerState().CombatEndServerSeconds <= 0.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingDamageLifetimeTest, "ProjectRPG.Riding.Damage.PawnAndIdentity", Flags)
bool FRidingDamageLifetimeTest::RunTest(const FString&)
{
    FFixture F;
    {
        FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC);
        F.A.PC->UnPossess(); F.A.Pawn = F.Spawn(); F.A.PC->Possess(F.A.Pawn);
        F.B.PC->UnPossess(); F.B.Pawn = F.Spawn(); F.B.PC->Possess(F.B.Pawn);
        RPGPrepareRidingMovementTestSession(*F.B.Session, *F.B.Riding()); // replaces only B's authenticated identity
        RPGPrepareRidingMovementTestSession(*F.A.Session, *F.A.Riding()); // separate replacement identity is tested below
        Scope.Complete(7.f, 0.f);
    }
    TestTrue(TEXT("Old outcome cannot poison replacement identities"), F.A.Mount() && F.B.Mount());
    auto* Definition = const_cast<URPGMountDefinition*>(F.A.Riding()->GetPresentationDefinition());
    {
        FRPGRidingDamageScope Scope(F.A.Pawn->ASC, F.B.Pawn->ASC);
        F.A.PC->UnPossess(); F.A.Pawn = F.Spawn(); F.A.PC->Possess(F.A.Pawn);
        F.A.Riding()->RefreshBinding();
        URPGMountDefinition* Definitions[] = {Definition};
        RPGSetRidingVisualTestState(*F.A.Riding(), F.A.Riding()->GetRidingState(), Definitions);
        TestFalse(TEXT("Original session barrier survives Pawn replacement"), F.A.Mount());
        Scope.Complete(0.f, 7.f);
    }
    TestTrue(TEXT("Same session replacement gets durable combat lock"), F.A.Session->GetOwnerState().CombatEndServerSeconds >= 9.9);
    TestFalse(TEXT("Replacement cannot mount after damage scope ends"), F.A.Mount());
    return true;
}
#endif
