#include "Riding/RPGRidingPresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGRidingActorTests.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGMountDefinition.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

struct FRPGRidingPresentationTestAccess
{
    static void Refresh(URPGRidingPresentationComponent& C) { C.Refresh(); }
    static FGuid Token(const URPGRidingPresentationComponent& C) { return C.LoadToken; }
    static void Complete(URPGRidingPresentationComponent& C, FGuid Token) { C.CompleteLoad(Token); }
    static USkeletalMeshComponent* Mesh(const URPGRidingPresentationComponent& C) { return C.MountMesh; }
    static USceneComponent* Root(const URPGRidingPresentationComponent& C) { return C.VisualRoot; }
    static UStaticMeshComponent* Placeholder(const URPGRidingPresentationComponent& C) { return C.Placeholder; }
};

namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const TCHAR* MeshPath = TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Character/Mesh/SK_Mannequin.SK_Mannequin");
    struct FVisualFixture
    {
        UWorld* World;
        ACharacter* Character;
        URPGRidingComponent* Riding;
        URPGRidingPresentationComponent* Visual;
        TArray<URPGMountDefinition*> Definitions;
        FRPGRidingRepState State;
        UStaticMeshComponent* OtherVisual;
        FTransform InitialActor, InitialMesh;
        float InitialRadius, InitialHeight;
        FVisualFixture()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            Character = World->SpawnActor<ACharacter>();
            Riding = NewObject<URPGRidingComponent>(Character); Riding->RegisterComponent();
            Visual = NewObject<URPGRidingPresentationComponent>(Character); Visual->RegisterComponent();
            OtherVisual = NewObject<UStaticMeshComponent>(Character); OtherVisual->SetupAttachment(Character->GetRootComponent()); OtherVisual->RegisterComponent();
            for (const auto* Id : {TEXT("horse"), TEXT("wolf")})
            {
                auto* D = NewObject<URPGMountDefinition>(Character);
                D->MountId = Id; D->DisplayName = FText::FromString(Id); D->SaddleSocket = TEXT("root");
                D->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(MeshPath));
                D->AnimClass = UAnimInstance::StaticClass(); D->RiderPoseLayer = UAnimInstance::StaticClass();
                D->Icon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture")));
                D->DashPresentation = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Fixture/Dash.Dash")));
                D->EmotePresentation = D->DashPresentation;
                D->MountOffset.SetTranslation(FVector(5, 0, 0));
                D->RiderOffset.SetTranslation(FVector(0, 0, 40));
                Definitions.Add(D);
            }
            InitialActor = Character->GetActorTransform(); InitialMesh = Character->GetMesh()->GetRelativeTransform();
            InitialRadius = Character->GetCapsuleComponent()->GetUnscaledCapsuleRadius(); InitialHeight = Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
            State.Key = {FGuid::NewGuid(), 1, 0}; State.Mode = ERPGRidingMode::Mounted; State.MountId = TEXT("horse"); State.DefinitionVersion = 1;
            State.Movement = {1, 600, 2048, 2000, 500};
            Set();
        }
        void Set() { RPGSetRidingVisualTestState(*Riding, State, Definitions); FRPGRidingPresentationTestAccess::Refresh(*Visual); }
        void Complete()
        {
            // Synchronous resolution is confined to the fixture; the product path always uses RequestAsyncLoad.
            LoadObject<USkeletalMesh>(nullptr, MeshPath);
            FRPGRidingPresentationTestAccess::Complete(*Visual, FRPGRidingPresentationTestAccess::Token(*Visual));
        }
        ~FVisualFixture() { World->DestroyWorld(false); }
    };
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FWaitRidingVisual, TSharedPtr<FVisualFixture>, Fixture, FAutomationTestBase*, Test);
bool FWaitRidingVisual::Update()
{
    static constexpr double Timeout = 40;
    // A per-command start is captured in the first queued update, not a world/client clock.
    if (!StartTime) StartTime = FPlatformTime::Seconds();
    const auto Status = Fixture->Visual->GetVisualStatus();
    if (Status == ERPGRidingVisualStatus::Loading && FPlatformTime::Seconds() - StartTime < Timeout) return false;
    Test->TestTrue(TEXT("Actual asynchronous asset load installs the current visual"), Status == ERPGRidingVisualStatus::Ready);
    Test->TestTrue(TEXT("Async-loaded saddle transform is available"), Fixture->Visual->GetPoseView().bHasAnchor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualAsync, "ProjectRPG.Riding.Presentation.Async", Flags)
bool FRidingVisualAsync::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FWaitRidingVisual(MakeShared<FVisualFixture>(), this));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualLifecycle, "ProjectRPG.Riding.Presentation.Lifecycle", Flags)
bool FRidingVisualLifecycle::RunTest(const FString&)
{
    FVisualFixture F; F.Complete();
    TestTrue(TEXT("Valid current assets produce Ready"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Ready);
    auto* Model = FRPGRidingPresentationTestAccess::Mesh(*F.Visual);
    if (!TestNotNull(TEXT("Cosmetic skeletal component created"), Model)) return false;
    TestTrue(TEXT("Visual follows character smoothing mesh"), Model->GetAttachParent()->GetAttachParent() == F.Character->GetMesh());
    TestTrue(TEXT("No cosmetic collision"), Model->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Model->GetGenerateOverlapEvents());
    TestTrue(TEXT("Saddle pose query available"), F.Visual->GetPoseView().bHasAnchor);
    TestFalse(TEXT("Ready model hides placeholder"), FRPGRidingPresentationTestAccess::Placeholder(*F.Visual)->IsVisible());
    const auto Token = FRPGRidingPresentationTestAccess::Token(*F.Visual);
    F.State.Key.ActionSequence++; F.Set();
    TestTrue(TEXT("Action notification does not reload same visual revision"), FRPGRidingPresentationTestAccess::Token(*F.Visual) == Token);
    FRPGRidingPresentationTestAccess::Complete(*F.Visual, Token);
    TestTrue(TEXT("Repeated completion is idempotent"), Model == FRPGRidingPresentationTestAccess::Mesh(*F.Visual));
    F.State.Mode = ERPGRidingMode::OnFoot; F.State.MountId.Reset(); F.State.DefinitionVersion = 0; F.State.Movement = {}; F.State.Key.StateRevision++; F.Set();
    TestTrue(TEXT("Dismount clears all owned visuals"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Hidden && !FRPGRidingPresentationTestAccess::Root(*F.Visual));
    FRPGRidingPresentationTestAccess::Complete(*F.Visual, Token);
    TestTrue(TEXT("Late completion cannot resurrect dismounted visual"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Hidden);
    TestTrue(TEXT("Other visual remains registered"), F.OtherVisual->IsRegistered());
    TestTrue(TEXT("Actor and mesh transforms preserved"), F.Character->GetActorTransform().Equals(F.InitialActor) && F.Character->GetMesh()->GetRelativeTransform().Equals(F.InitialMesh));
    TestEqual(TEXT("Capsule radius preserved"), F.Character->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), F.InitialRadius);
    TestEqual(TEXT("Capsule height preserved"), F.Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), F.InitialHeight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualReplacement, "ProjectRPG.Riding.Presentation.Replacement", Flags)
bool FRidingVisualReplacement::RunTest(const FString&)
{
    FVisualFixture F;
    const auto OldToken = FRPGRidingPresentationTestAccess::Token(*F.Visual);
    F.State.MountId = TEXT("wolf"); F.State.Key.StateRevision++; F.Set();
    const auto CurrentToken = FRPGRidingPresentationTestAccess::Token(*F.Visual);
    FRPGRidingPresentationTestAccess::Complete(*F.Visual, OldToken);
    TestTrue(TEXT("Old mount completion is ignored"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Loading);
    TestTrue(TEXT("Replacement has a new generation"), OldToken != CurrentToken);
    F.Complete();
    TestTrue(TEXT("Current replacement installs"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Ready);
    F.State.Key.RiderEpoch = FGuid::NewGuid(); F.State.Key.StateRevision = 1; F.Set();
    FRPGRidingPresentationTestAccess::Complete(*F.Visual, CurrentToken);
    TestTrue(TEXT("Earlier epoch cannot install"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Loading);
    F.Complete();
    F.Definitions[1]->DefinitionVersion++;
    FRPGRidingPresentationTestAccess::Refresh(*F.Visual);
    TestTrue(TEXT("Definition version mismatch stays placeholder"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Failed);
    F.State.DefinitionVersion++; F.State.Key.StateRevision++; F.Set(); F.Complete();
    TestTrue(TEXT("New matching definition can recover"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Ready);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualFailure, "ProjectRPG.Riding.Presentation.Failure", Flags)
bool FRidingVisualFailure::RunTest(const FString&)
{
    FVisualFixture F;
    F.Definitions[0]->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
    F.Set(); F.Complete();
    TestTrue(TEXT("Wrong asset type fails only cosmetics"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Failed);
    TestTrue(TEXT("Failed model retains placeholder visibility"), FRPGRidingPresentationTestAccess::Placeholder(*F.Visual)->IsVisible());
    TestTrue(TEXT("Failure preserves server-mounted state"), F.Riding->GetRidingState().Mode == ERPGRidingMode::Mounted);
    F.Definitions[0]->Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(MeshPath));
    F.Visual->RetryPresentation(); F.Complete();
    TestTrue(TEXT("Explicit cosmetic retry recovers"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Ready);
    F.Definitions[0]->SaddleSocket = TEXT("missing_saddle"); F.Set(); F.Complete();
    TestTrue(TEXT("Missing saddle does not install a broken model"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Failed
        && !FRPGRidingPresentationTestAccess::Mesh(*F.Visual));
    F.Definitions[0]->SaddleSocket = TEXT("root"); F.Visual->RetryPresentation(); F.Complete();
    const auto Token = FRPGRidingPresentationTestAccess::Token(*F.Visual);
    F.Visual->UnregisterComponent();
    FRPGRidingPresentationTestAccess::Complete(*F.Visual, Token);
    TestTrue(TEXT("Unregistered component rejects late callback"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Hidden);
    F.Visual->RegisterComponent(); FRPGRidingPresentationTestAccess::Refresh(*F.Visual); F.Complete();
    TestTrue(TEXT("Re-registration rebuilds current snapshot"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Ready);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualReentry, "ProjectRPG.Riding.Presentation.Reentry", Flags)
bool FRidingVisualReentry::RunTest(const FString&)
{
    FVisualFixture F;
    F.Definitions[0]->AnimClass = URPGRidingTestReentryAnim::StaticClass();
    F.Set(); F.Complete();
    TestTrue(TEXT("Animation initialization may end Riding without resurrecting the visual"), F.Visual->GetVisualStatus() == ERPGRidingVisualStatus::Hidden);
    TestTrue(TEXT("Reentrant end releases owned model/root"), !FRPGRidingPresentationTestAccess::Root(*F.Visual)
        && !FRPGRidingPresentationTestAccess::Mesh(*F.Visual));
    TestTrue(TEXT("Latest state remains on foot"), F.Riding->GetRidingState().Mode == ERPGRidingMode::OnFoot);
    return true;
}
#endif
