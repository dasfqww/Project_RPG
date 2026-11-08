#include "Tests/RPGRidingActorTests.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingRegionVolume.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"

ARPGRidingTestPawn::ARPGRidingTestPawn()
{
    ASC = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("TestASC"));
    Attributes = CreateDefaultSubobject<URPGRidingTestAttributes>(TEXT("TestAttributes"));
    CreateDefaultSubobject<URPGRidingComponent>(TEXT("Riding"));
}

void URPGRidingTestReentryAnim::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
#if WITH_DEV_AUTOMATION_TESTS
    auto* Actor = GetOwningActor();
    auto* C = Actor ? Actor->FindComponentByClass<URPGRidingComponent>() : nullptr;
    if (!C || C->GetRidingState().Mode != ERPGRidingMode::Mounted) return;
    FRPGRidingRepState OnFoot;
    OnFoot.Key = C->GetRidingState().Key; ++OnFoot.Key.StateRevision;
    RPGSetRidingVisualTestState(*C, OnFoot, {});
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

struct FRPGRidingActorTestAccess
{
    static void Configure(URPGRidingSessionComponent& S)
    {
        S.Policy = NewObject<URPGRidingPolicy>(&S);
        S.Policy->Values.GameplayHash = FString::ChrN(64, TEXT('a'));
        S.ValidateAdmission = []() { return true; };
        S.UpdateAuthenticatedContext(FGuid::NewGuid().ToString(), TEXT("test-steam"), FGuid::NewGuid().ToString(), 1, false, true);
        FRPGRidingAdmissionSnapshot Snapshot;
        FGuid Character, Dungeon; FGuid::Parse(S.BoundCharacter, Character); FGuid::Parse(S.BoundSession, Dungeon);
        Snapshot.Identity = {S.AuthorityContext, Character, Dungeon, S.BoundGeneration};
        Snapshot.RosterId = FGuid::NewGuid(); Snapshot.ProtocolVersion = 1; Snapshot.CatalogVersion = 1;
        Snapshot.GameplayHash = S.Policy->Values.GameplayHash; Snapshot.OwnedMountIds = {TEXT("horse"), TEXT("wolf")};
        Snapshot.SelectedMountId = TEXT("horse"); Snapshot.Favorites = {TEXT("horse"), TEXT("wolf")};
        const auto Ticket = S.Runtime->BeginSnapshot(FPlatformTime::Seconds());
        check(S.Runtime->ApplySnapshot(Ticket, Snapshot, FPlatformTime::Seconds()));
        S.PublishOwnerState();
    }
    static void Define(URPGRidingComponent& C)
    {
        for (const auto* Id : {TEXT("horse"), TEXT("wolf")})
        {
            auto* D = NewObject<URPGMountDefinition>(&C);
            D->MountId = Id; D->DisplayName = FText::FromString(Id); D->SaddleSocket = TEXT("saddle");
            D->Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Test/Icon.Icon")));
            D->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Test/Mesh.Mesh")));
            D->AnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/Test/Anim.Anim_C")));
            D->RiderPoseLayer = D->AnimClass;
            D->DashPresentation = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Test/Dash.Dash")));
            D->EmotePresentation = D->DashPresentation;
            C.Definitions.Add(D);
        }
    }
    static void Send(URPGRidingComponent& C, const FRPGRidingSetCommand& Command) { C.ServerSetRiding_Implementation(Command); }
    static void Revoke(URPGRidingSessionComponent& S) { S.Invalidate(true); }
    static void Result(URPGRidingComponent& C, FRPGRidingCommandResult Value) { C.ClientRidingResult_Implementation(Value); }
    static void Replicate(URPGRidingComponent& C, const FRPGRidingRepState& Incoming)
    { const auto Old = C.State; C.State = Incoming; C.OnRep_State(Old); }
    static void VisualState(URPGRidingComponent& C, const FRPGRidingRepState& State, TConstArrayView<URPGMountDefinition*> Definitions)
    {
        C.Definitions.Reset();
        for (auto* Definition : Definitions) C.Definitions.Add(Definition);
        Replicate(C, State);
    }
};

void RPGSetRidingVisualTestState(URPGRidingComponent& Component, const FRPGRidingRepState& State,
    TConstArrayView<URPGMountDefinition*> Definitions)
{
    FRPGRidingActorTestAccess::VisualState(Component, State, Definitions);
}

void RPGPrepareRidingMovementTestSession(URPGRidingSessionComponent& Session, URPGRidingComponent& Riding)
{
    auto* Area = Riding.GetWorld()->SpawnActor<ARPGRidingRegionVolume>();
    Area->Bounds->SetBoxExtent(FVector(10000));
    FRPGRidingActorTestAccess::Configure(Session);
    FRPGRidingActorTestAccess::Define(Riding);
    Riding.RefreshBinding();
}
void RPGSendRidingMovementTestCommand(URPGRidingComponent& Riding, const FRPGRidingSetCommand& Command)
{
    FRPGRidingActorTestAccess::Send(Riding, Command);
}

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    struct FActors
    {
        UWorld* World;
        APlayerController* Controller;
        APlayerState* PlayerState;
        ARPGRidingTestPawn* Pawn;
        URPGRidingSessionComponent* Session;
        URPGRidingComponent* Riding;
        FActors()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            auto* Area = World->SpawnActor<ARPGRidingRegionVolume>();
            Area->Bounds->SetBoxExtent(FVector(10000));
            Controller = World->SpawnActor<APlayerController>();
            PlayerState = World->SpawnActor<APlayerState>();
            Controller->SetPlayerState(PlayerState); PlayerState->SetOwner(Controller);
            Session = NewObject<URPGRidingSessionComponent>(PlayerState); Session->RegisterComponent();
            FRPGRidingActorTestAccess::Configure(*Session);
            Pawn = SpawnPawn(); Controller->Possess(Pawn);
            Riding = Pawn->FindComponentByClass<URPGRidingComponent>();
            Riding->RefreshBinding();
        }
        ARPGRidingTestPawn* SpawnPawn()
        {
            auto* Value = World->SpawnActor<ARPGRidingTestPawn>();
            Value->ASC->InitAbilityActorInfo(Value, Value);
            Value->ASC->AddAttributeSetSubobject(Value->Attributes.Get());
            Value->Attributes->CurrentHealth.SetBaseValue(100); Value->Attributes->CurrentHealth.SetCurrentValue(100);
            Value->GetCharacterMovement()->MovementMode = MOVE_Walking;
            FRPGRidingActorTestAccess::Define(*Value->FindComponentByClass<URPGRidingComponent>());
            return Value;
        }
        FRPGRidingSetCommand Command(uint32 Sequence, bool bMounted = true) const
        {
            FRPGRidingSetCommand Value; Value.RiderEpoch = Riding->GetRidingState().Key.RiderEpoch;
            Value.ExpectedStateRevision = Riding->GetRidingState().Key.StateRevision;
            Value.RequestSeq = Sequence; Value.bDesiredMounted = bMounted;
            return Value;
        }
        ~FActors() { World->DestroyWorld(false); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingActorTransitions, "ProjectRPG.Riding.Actors.Transitions", Flags)
bool FRidingActorTransitions::RunTest(const FString&)
{
    FActors F;
    TestTrue(TEXT("Confirmed identity creates epoch"), F.Riding->GetRidingState().Key.RiderEpoch.IsValid());
    const auto First = F.Command(1);
    FRPGRidingActorTestAccess::Send(*F.Riding, First);
    TestTrue(TEXT("Selected owned mount applied"), F.Riding->GetRidingState().MountId == TEXT("horse"));
    const auto Revision = F.Riding->GetRidingState().Key.StateRevision;
    FRPGRidingActorTestAccess::Send(*F.Riding, First);
    TestEqual(TEXT("Duplicate does not reexecute"), F.Riding->GetRidingState().Key.StateRevision, Revision);
    auto Replace = F.Command(2); Replace.Selector.Kind = ERPGRidingSelectorKind::Specific; Replace.Selector.MountId = TEXT("wolf");
    FRPGRidingActorTestAccess::Send(*F.Riding, Replace);
    TestTrue(TEXT("Owned replacement applied"), F.Riding->GetRidingState().MountId == TEXT("wolf"));
    auto Unowned = F.Command(3); Unowned.Selector.Kind = ERPGRidingSelectorKind::Specific; Unowned.Selector.MountId = TEXT("forged");
    FRPGRidingActorTestAccess::Send(*F.Riding, Unowned);
    TestTrue(TEXT("Unowned replacement preserves current"), F.Riding->GetRidingState().MountId == TEXT("wolf"));
    F.Pawn->ASC->ClearActorInfo();
    FRPGRidingActorTestAccess::Send(*F.Riding, F.Command(4, false));
    TestTrue(TEXT("Unready ASC still permits manual dismount"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingActorLifetime, "ProjectRPG.Riding.Actors.Lifetime", Flags)
bool FRidingActorLifetime::RunTest(const FString&)
{
    FActors F;
    const auto OldCommand = F.Command(1);
    FRPGRidingActorTestAccess::Send(*F.Riding, OldCommand);
    auto* OldRiding = F.Riding;
    auto* Replacement = F.SpawnPawn();
    F.Controller->Possess(Replacement);
    OldRiding->RefreshBinding();
    F.Riding = Replacement->FindComponentByClass<URPGRidingComponent>();
    F.Riding->RefreshBinding();
    TestTrue(TEXT("Old Pawn clears mounted state"), OldRiding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestTrue(TEXT("Replacement starts on foot with new epoch"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot
        && F.Riding->GetRidingState().Key.RiderEpoch != OldCommand.RiderEpoch);
    FRPGRidingActorTestAccess::Send(*OldRiding, OldCommand);
    TestTrue(TEXT("Old actor cannot affect replacement"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    FRPGRidingActorTestAccess::Send(*F.Riding, F.Command(1));
    Replacement->Attributes->CurrentHealth.SetCurrentValue(0);
    F.Riding->TickComponent(0.1f, LEVELTICK_All, nullptr);
    TestTrue(TEXT("GAS health death forces dismount"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    FRPGRidingActorTestAccess::Revoke(*F.Session);
    F.Riding->RefreshBinding();
    TestFalse(TEXT("Revocation clears epoch and prevents rebind"), F.Riding->GetRidingState().Key.RiderEpoch.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingActorReentry, "ProjectRPG.Riding.Actors.Reentry", Flags)
bool FRidingActorReentry::RunTest(const FString&)
{
    FActors F;
    F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    { if (State.Mode == ERPGRidingMode::Mounted) F.Riding->ForceDismount(ERPGRidingEndReason::Hit); });
    FRPGRidingActorTestAccess::Send(*F.Riding, F.Command(1));
    TestTrue(TEXT("Reentrant force end is retained"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    TestTrue(TEXT("Force reason remains current"), F.Riding->GetRidingState().LastEndReason == ERPGRidingEndReason::Hit);
    F.Riding->OnChanged.Clear();
    F.Riding->OnChanged.AddLambda([&](const FRPGRidingRepState& State)
    { if (State.Mode == ERPGRidingMode::Mounted) FRPGRidingActorTestAccess::Revoke(*F.Session); });
    FRPGRidingActorTestAccess::Send(*F.Riding, F.Command(2));
    TestFalse(TEXT("Revocation during transition cannot publish stale mounted state"), F.Riding->GetRidingState().Key.RiderEpoch.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingActorResults, "ProjectRPG.Riding.Actors.Results", Flags)
bool FRidingActorResults::RunTest(const FString&)
{
    FActors F;
    const auto Initial = F.Riding->GetRidingState();
    FRPGRidingActorTestAccess::Send(*F.Riding, F.Command(1));
    const auto Mounted = F.Riding->GetRidingState();
    FRPGRidingCommandResult Result; Result.LatestState = Initial; Result.HistoricalState = Initial;
    FRPGRidingActorTestAccess::Result(*F.Riding, Result);
    TestEqual(TEXT("Historical result never installs older authority"), F.Riding->GetRidingState().Key.StateRevision, Mounted.Key.StateRevision);
    FRPGRidingActorTestAccess::Replicate(*F.Riding, Initial);
    TestEqual(TEXT("Old same-epoch replication ignored"), F.Riding->GetRidingState().Key.StateRevision, Mounted.Key.StateRevision);
    return true;
}
#endif
