#include "Tests/RPGRidingMovementTests.h"
#include "Tests/RPGRidingActorTests.h"
#include "Component/RPGPlayerMovementComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "Tests/RPGTestWorldCleanup.h"

ARPGRidingMovementTestPawn::ARPGRidingMovementTestPawn(const FObjectInitializer& Initializer)
    : Super(Initializer.SetDefaultSubobjectClass<URPGPlayerMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ASC"));
    Attributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("Attributes"));
    CreateDefaultSubobject<URPGRidingComponent>(TEXT("Riding"));
}
UAbilitySystemComponent* ARPGRidingMovementTestPawn::GetAbilitySystemComponent() const { return ASC; }

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Serialization/BitWriter.h"
#include "Serialization/BitReader.h"
#include "HAL/IConsoleManager.h"
#include <limits>

struct FRPGRidingMovementTestAccess
{
    static bool Correct(URPGRidingComponent& C, const FRPGRidingRepState& State) { return C.ApplyMovementCorrection(State); }
    static bool StampError(URPGPlayerMovementComponent& C, const FRPGRidingMoveStamp& Stamp)
    {
        TGuardValue<bool> Evaluating(C.bEvaluatingRidingMove, true), Known(C.bHasRidingStamp, true);
        TGuardValue<FRPGRidingMoveStamp> Current(C.EvaluatedStamp, Stamp);
        FMovementBaseInterfaceData Base;
        return C.ServerCheckClientError(1, 0.01f, FVector::ZeroVector, C.GetOwner()->GetActorLocation(), FVector::ZeroVector, &Base, NAME_None, C.PackNetworkMovementMode());
    }
};

namespace RPGRidingMovementTests
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        UWorld* World;
        APlayerController* Controller;
        APlayerState* PlayerState;
        ARPGRidingMovementTestPawn* Pawn;
        URPGRidingComponent* Riding;
        URPGPlayerMovementComponent* Move;
        bool bPhysics;
        uint64 Frame = GFrameCounter;
        explicit FFixture(bool Physics = false) : bPhysics(Physics)
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(Physics).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(Physics);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, Physics ? nullptr : &Init);
            if (Physics)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
                auto* Ground = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Ground);
                Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
                Box->SetBoxExtent(FVector(10000, 10000, 50)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
                Box->RegisterComponent(); Ground->SetActorLocation(FVector(0, 0, -50));
            }
            Controller = World->SpawnActor<APlayerController>(); PlayerState = World->SpawnActor<APlayerState>();
            if (Physics) Controller->SetAsLocalPlayerController();
            Controller->SetPlayerState(PlayerState); PlayerState->SetOwner(Controller);
            auto* Session = NewObject<URPGRidingSessionComponent>(PlayerState); Session->RegisterComponent();
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Pawn = World->SpawnActor<ARPGRidingMovementTestPawn>(FVector(0, 0, Physics ? 200 : 0), FRotator::ZeroRotator, Spawn);
            Controller->Possess(Pawn);
            Pawn->ASC->InitAbilityActorInfo(Pawn, Pawn); Pawn->ASC->AddAttributeSetSubobject(Pawn->Attributes.Get());
            Pawn->Attributes->InitCurrentHealth(100); Pawn->Attributes->InitMoveSpeed(2); Pawn->Attributes->InitMoveSpeedPercent(25);
            Move = CastChecked<URPGPlayerMovementComponent>(Pawn->GetCharacterMovement());
            Move->bRunPhysicsWithNoController = true;
            Move->MovementMode = MOVE_Walking; Move->MaxWalkSpeed = 400;
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>();
            RPGPrepareRidingMovementTestSession(*Session, *Riding);
            if (Physics) { Move->SetMovementMode(MOVE_Falling); Tick(0.6f); }
        }
        void Mount()
        {
            FRPGRidingSetCommand C; C.RiderEpoch = Riding->GetRidingState().Key.RiderEpoch;
            C.ExpectedStateRevision = Riding->GetRidingState().Key.StateRevision; C.RequestSeq = 1; C.bDesiredMounted = true;
            RPGSendRidingMovementTestCommand(*Riding, C);
        }
        void Tick(float Seconds, bool bInput = false)
        {
            for (int32 Index = 0; Index < FMath::CeilToInt(Seconds * 60); ++Index)
            {
                Frame = FMath::Max(Frame, GFrameCounter) + 1; TGuardValue<uint64> Guard(GFrameCounter, Frame);
                if (bInput) Pawn->AddMovementInput(FVector::ForwardVector);
                World->Tick(LEVELTICK_All, 1.f / 60);
            }
        }
        ~FFixture() { Pawn->SetRole(ROLE_Authority); if (bPhysics) RPGTestWorld::Destroy(World); else World->DestroyWorld(false); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingMovementProfileTest, "ProjectRPG.Riding.Movement.Profile", RPGRidingMovementTests::Flags)
bool FRidingMovementProfileTest::RunTest(const FString&)
{
    RPGRidingMovementTests::FFixture F;
    TestEqual(TEXT("Original foot GAS formula"), F.Move->GetMaxSpeed(), 1000.f);
    F.Mount();
    TestTrue(TEXT("Mounted state has admitted tuning"), F.Riding->GetRidingState().Movement.IsValid());
    TestEqual(TEXT("Mounted excludes combat multipliers"), F.Move->GetMaxSpeed(), 600.f);
    const auto Limit = F.Move->BeginWalkSpeedLimit(300);
    TestEqual(TEXT("External absolute cap precedes Riding"), F.Move->GetMaxSpeed(), 300.f);
    F.Move->EndWalkSpeedLimit(Limit);
    TestEqual(TEXT("Riding acceleration"), F.Move->GetMaxAcceleration(), 2048.f);
    TestEqual(TEXT("Riding braking"), F.Move->GetMaxBrakingDeceleration(), 2000.f);
    TestEqual(TEXT("Riding yaw delta"), F.Move->GetDeltaRotation(0.1f).Yaw, 50.0);
    TestFalse(TEXT("Mounted cannot jump"), F.Move->CanAttemptJump());
    TestFalse(TEXT("Mounted cannot crouch"), F.Move->CanCrouchInCurrentState());
    const auto Constraint = F.Move->BeginSkillMovementConstraint(F.Pawn, 0);
    TestEqual(TEXT("External movement lock remains effective"), F.Move->GetMaxSpeed(), 0.f);
    F.Move->EndSkillMovementConstraint(Constraint);
    F.Move->MaxWalkSpeed = 720;
    F.Pawn->Attributes->InitMoveSpeed(1.5f); F.Pawn->Attributes->InitMoveSpeedPercent(0);
    F.Riding->ForceDismount(ERPGRidingEndReason::Manual);
    TestEqual(TEXT("Dismount uses current foot configuration, not cached values"), F.Move->GetMaxSpeed(), 1080.f);
    TestTrue(TEXT("Foot clears admitted profile"), F.Riding->GetRidingState().Movement.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingMovementSavedTest, "ProjectRPG.Riding.Movement.SavedMoves", RPGRidingMovementTests::Flags)
bool FRidingMovementSavedTest::RunTest(const FString&)
{
    RPGRidingMovementTests::FFixture F;
    auto* Data = static_cast<FPredictionData_Client_RPGRiding*>(F.Move->GetPredictionData_Client());
    auto Old = Data->AllocateNewMove(); auto Next = Data->AllocateNewMove();
    Old->SetMoveFor(F.Pawn, 0.01f, FVector::ZeroVector, *Data);
    F.Mount(); Next->SetMoveFor(F.Pawn, 0.01f, FVector::ZeroVector, *Data);
    TestFalse(TEXT("Cannot combine across mount boundary"), Old->CanCombineWith(Next, F.Pawn, 0.1f));
    const auto Mounted = F.Riding->GetRidingState();
    F.Riding->ForceDismount(ERPGRidingEndReason::Hit);
    const auto Current = F.Riding->GetRidingState();
    Next->PrepMoveFor(F.Pawn);
    TestTrue(TEXT("Old replay never restores Mounted"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestEqual(TEXT("Replay preserves current authority revision"), F.Riding->GetRidingState().Key.StateRevision, Current.Key.StateRevision);
    auto& Saved = static_cast<FSavedMove_RPGRiding&>(*Next);
    auto New = Data->AllocateNewMove(); auto& NewSaved = static_cast<FSavedMove_RPGRiding&>(*New);
    Saved.Riding = FRPGRidingMoveStamp::FromState(Mounted); NewSaved.Riding = Saved.Riding;
    NewSaved.Riding.Key.ActionSequence++;
    TestFalse(TEXT("Action boundary prevents combine"), Saved.CanCombineWith(New, F.Pawn, 0.1f));
    NewSaved.Riding = Saved.Riding; NewSaved.Riding.ProfileVersion++;
    TestFalse(TEXT("Profile boundary prevents combine"), Saved.CanCombineWith(New, F.Pawn, 0.1f));
    Saved.Clear(); TestTrue(TEXT("Pooled move clears Riding stamp"), Saved.Riding == FRPGRidingMoveStamp{});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingMovementWireTest, "ProjectRPG.Riding.Movement.Wire", RPGRidingMovementTests::Flags)
bool FRidingMovementWireTest::RunTest(const FString&)
{
    RPGRidingMovementTests::FFixture F; F.Mount();
    auto State = F.Riding->GetRidingState();
    auto Stamp = FRPGRidingMoveStamp::FromState(State);
    FBitWriter Bits(4096, true); TestTrue(TEXT("Stamp encodes"), Stamp.Serialize(Bits));
    FBitReader Read(Bits.GetData(), Bits.GetNumBits()); FRPGRidingMoveStamp Decoded;
    TestTrue(TEXT("Stamp decodes"), Decoded.Serialize(Read) && Decoded == Stamp);
    TestTrue(TEXT("Inactive stamp bounded to 30 bytes"), Bits.GetNumBits() <= 240);
    FRPGRidingNetworkMoveData Packet; Packet.Riding = Stamp; Packet.TimeStamp = 1;
    Packet.MovementMode = F.Move->PackNetworkMovementMode();
    FBitWriter Packed(8192, true);
    TestTrue(TEXT("Actual packed move serializer encodes"), Packet.Serialize(*F.Move, Packed, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
    FBitReader PackedRead(Packed.GetData(), Packed.GetNumBits()); FRPGRidingNetworkMoveData PacketCopy;
    TestTrue(TEXT("Actual packed move serializer decodes"), PacketCopy.Serialize(*F.Move, PackedRead, nullptr, FCharacterNetworkMoveData::ENetworkMoveType::NewMove));
    TestTrue(TEXT("Packed move preserves identifiers"), PacketCopy.Riding == Stamp);
    FBitWriter Response(4096, true); TestTrue(TEXT("Authority state encodes"), FRPGRidingMoveResponse::SerializeState(Response, State));
    FBitReader StateRead(Response.GetData(), Response.GetNumBits()); FRPGRidingRepState Copy;
    TestTrue(TEXT("State decodes"), FRPGRidingMoveResponse::SerializeState(StateRead, Copy));
    TestEqual(TEXT("Profile round trip"), Copy.Movement.Speed, 600.f);
    TestTrue(TEXT("No references or unbounded strings needed"), Response.GetNumBits() <= 2048);
    FClientAdjustment Adjustment;
    Adjustment.bAckGoodMove = false; Adjustment.TimeStamp = 1;
    Adjustment.NewLoc = F.Pawn->GetActorLocation(); Adjustment.MovementMode = F.Move->PackNetworkMovementMode();
    FRPGRidingMoveResponse FullResponse; FullResponse.ServerFillResponseData(*F.Move, Adjustment);
    FBitWriter FullBits(8192, true);
    TestTrue(TEXT("Actual correction serializer encodes"), FullResponse.Serialize(*F.Move, FullBits, nullptr));
    FRPGRidingMoveResponse FullCopy; FBitReader FullRead(FullBits.GetData(), FullBits.GetNumBits());
    TestTrue(TEXT("Actual correction serializer decodes"), FullCopy.Serialize(*F.Move, FullRead, nullptr));
    TestTrue(TEXT("Correction carries current approved state"), FullCopy.bHasRidingState
        && FRPGRidingMoveStamp::FromState(FullCopy.State) == Stamp && FullCopy.State.Movement.Speed == 600.f);
    Adjustment.bAckGoodMove = true; FullResponse.ServerFillResponseData(*F.Move, Adjustment);
    TestFalse(TEXT("Good move acknowledgement omits authority state"), FullResponse.bHasRidingState);
    FBitReader Truncated(Response.GetData(), 9); FRPGRidingRepState Missing;
    auto* EnsureOverflow = IConsoleManager::Get().FindConsoleVariable(TEXT("net.BitReader.EnsureOnOverflow"));
    const int32 PreviousEnsure = EnsureOverflow ? EnsureOverflow->GetInt() : 0;
    if (EnsureOverflow) EnsureOverflow->Set(0, ECVF_SetByCode);
    AddExpectedError(TEXT("FBitReader::SetOverflowed() called!"), EAutomationExpectedErrorFlags::Contains, 1, false);
    TestFalse(TEXT("Truncated state rejected"), FRPGRidingMoveResponse::SerializeState(Truncated, Missing));
    if (EnsureOverflow) EnsureOverflow->Set(PreviousEnsure, ECVF_SetByCode);
    State.Movement.Speed = std::numeric_limits<float>::quiet_NaN();
    FBitWriter Invalid(4096, true); TestFalse(TEXT("Invalid authoritative profile rejected"), FRPGRidingMoveResponse::SerializeState(Invalid, State));
    State = Copy; State.MountId = FString::ChrN(65, TEXT('a'));
    FBitWriter Long(4096, true); TestFalse(TEXT("Oversize ID rejected before serialization"), FRPGRidingMoveResponse::SerializeState(Long, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingMovementCorrectionTest, "ProjectRPG.Riding.Movement.Correction", RPGRidingMovementTests::Flags)
bool FRidingMovementCorrectionTest::RunTest(const FString&)
{
    RPGRidingMovementTests::FFixture F; F.Mount();
    const auto Mounted = F.Riding->GetRidingState();
    auto Foot = FRPGRidingRepState{}; Foot.Key = Mounted.Key; Foot.Key.StateRevision++; Foot.LastEndReason = ERPGRidingEndReason::Hit;
    TestTrue(TEXT("Server refuses correction setter"), !FRPGRidingMovementTestAccess::Correct(*F.Riding, Foot));
    F.Pawn->SetRole(ROLE_AutonomousProxy);
    auto Other = Foot; Other.Key.RiderEpoch = FGuid::NewGuid();
    TestFalse(TEXT("Response cannot adopt another epoch"), FRPGRidingMovementTestAccess::Correct(*F.Riding, Other));
    TestTrue(TEXT("Same-epoch current authority applies before replay"), FRPGRidingMovementTestAccess::Correct(*F.Riding, Foot));
    TestFalse(TEXT("Old response cannot resurrect mounted profile"), FRPGRidingMovementTestAccess::Correct(*F.Riding, Mounted));
    TestEqual(TEXT("Foot formula restored after correction"), F.Move->GetMaxSpeed(), 1000.f);
    RPGSetRidingVisualTestState(*F.Riding, Mounted, {});
    TestTrue(TEXT("Older same-epoch RepNotify cannot overwrite response"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingMovementServerTest, "ProjectRPG.Riding.Movement.ServerGuards", RPGRidingMovementTests::Flags)
bool FRidingMovementServerTest::RunTest(const FString&)
{
    {
    RPGRidingMovementTests::FFixture F; F.Mount();
    auto Forged = FRPGRidingMoveStamp::FromState(F.Riding->GetRidingState()); Forged.ProfileVersion++;
    TestTrue(TEXT("Profile mismatch requests correction even with matching location"), FRPGRidingMovementTestAccess::StampError(*F.Move, Forged));
    TestEqual(TEXT("Received stamp cannot select speed"), F.Move->GetMaxSpeed(), 600.f);
    F.Move->SetMovementMode(MOVE_Falling);
    TestTrue(TEXT("Ground departure closes mounted state immediately"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestTrue(TEXT("Falling retains engine movement mode"), F.Move->MovementMode == MOVE_Falling);
    F.Pawn->Attributes->InitCurrentHealth(0);
    TestEqual(TEXT("Death takes precedence over foot and mounted speeds"), F.Move->GetMaxSpeed(), 0.f);
    }
    RPGRidingMovementTests::FFixture F(true);
    if (!TestTrue(FString::Printf(TEXT("Fixture reaches physical floor (mode=%d z=%.2f vz=%.2f)"), int32(F.Move->MovementMode), F.Pawn->GetActorLocation().Z, F.Move->Velocity.Z), F.Move->IsMovingOnGround())) return false;
    F.Mount();
    if (!TestTrue(TEXT("Mount approved before movement"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted)) return false;
    const float Before = F.Pawn->GetActorLocation().X;
    F.Tick(2, true);
    const float Distance = F.Pawn->GetActorLocation().X - Before;
    TestTrue(TEXT("Actual grounded input moves at Riding scale"), Distance > 900 && Distance < 1250);
    TestTrue(TEXT("Actual velocity respects 600 speed"), F.Move->Velocity.Size2D() <= 601 && F.Move->Velocity.Size2D() > 590);
    const auto Cap = F.Move->BeginWalkSpeedLimit(300);
    const float CappedStart = F.Pawn->GetActorLocation().X; F.Tick(1, true);
    TestTrue(TEXT("Actual displacement respects external cap"), F.Pawn->GetActorLocation().X - CappedStart <= 360);
    F.Move->EndWalkSpeedLimit(Cap);
    F.Move->MaxWalkSpeed = 720;
    F.Riding->ForceDismount(ERPGRidingEndReason::Manual);
    TestEqual(TEXT("Live base and GAS values survive mounted simulation"), F.Move->GetMaxSpeed(), 1800.f);
    return true;
}
#endif
