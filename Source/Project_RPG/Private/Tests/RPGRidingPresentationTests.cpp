#include "Riding/RPGRidingPresentationComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGRidingActorTests.h"
#include "Riding/RPGRidingAnimFrame.h"
#include "Async/Async.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGMountDefinition.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
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
    static UAnimMontage* Montage(const URPGRidingPresentationComponent& C) { return C.ActionMontage; }
    static FGuid ActionToken(const URPGRidingPresentationComponent& C) { return C.ActionToken; }
    static int32 ActionId(const URPGRidingPresentationComponent& C) { return C.ActionInstanceId; }
    static void CompleteAction(URPGRidingPresentationComponent& C, FGuid T) { C.CompleteActionPresentation(T); }
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualEmote, "ProjectRPG.Riding.Presentation.EmotePlayback", Flags)
bool FRidingVisualEmote::RunTest(const FString&)
{
    FVisualFixture F;F.Complete();
    auto* Sequence=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle"));
    if(!TestNotNull(TEXT("Read-only native animation fixture"),Sequence))return false;
    auto* Montage=UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence,TEXT("DefaultSlot"));
    auto* FixtureModel=FRPGRidingPresentationTestAccess::Mesh(*F.Visual);FixtureModel->InitAnim(true);
    if(!TestTrue(FString::Printf(TEXT("Fixture readiness: Anim=%d Root=%d Length=%.3f"),FixtureModel->GetAnimInstance()!=nullptr,Montage&&Montage->HasRootMotion(),Montage?Montage->GetPlayLength():0.f),
        FixtureModel->GetAnimInstance()&&Montage&&!Montage->HasRootMotion()&&Montage->GetPlayLength()>0))return false;
    F.Definitions[0]->EmotePresentation=Montage;
    ++F.State.Key.ActionSequence;F.State.Action=ERPGRidingAction::Emote;F.State.ExecutionId=FGuid::NewGuid();
    F.State.ActionDuration=3;F.State.ActionStartServerWorldSeconds=F.World->GetTimeSeconds();F.Set();
    const auto Id=FRPGRidingPresentationTestAccess::ActionId(*F.Visual);
    auto* Owned=FRPGRidingPresentationTestAccess::Montage(*F.Visual);
    TestTrue(TEXT("Actual montage uses an owned execution instance"),Owned==Montage&&Id!=INDEX_NONE);
    auto* Model=FRPGRidingPresentationTestAccess::Mesh(*F.Visual);
    TestTrue(TEXT("Montage playback created on the private mount model"),Model&&Model->GetAnimInstance()->GetMontageInstanceForID(Id));
    F.Set();TestEqual(TEXT("Repeated state does not restart montage"),FRPGRidingPresentationTestAccess::ActionId(*F.Visual),Id);
    const auto Token=FRPGRidingPresentationTestAccess::ActionToken(*F.Visual);
    F.State.Action=ERPGRidingAction::None;F.State.ExecutionId.Invalidate();F.State.ActionDuration=0;F.State.ActionStartServerWorldSeconds=0;++F.State.Key.ActionSequence;F.Set();
    FRPGRidingPresentationTestAccess::CompleteAction(*F.Visual,Token);
    TestTrue(TEXT("Old load cannot revive an ended action"),!FRPGRidingPresentationTestAccess::Montage(*F.Visual)&&!Model->GetAnimInstance()->Montage_IsPlaying(Owned));
    TestTrue(TEXT("Visual action never moves character"),F.Character->GetActorTransform().Equals(F.InitialActor));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualEmoteForeign, "ProjectRPG.Riding.Presentation.EmoteOwnership", Flags)
bool FRidingVisualEmoteForeign::RunTest(const FString&)
{
    FVisualFixture F;F.Complete();
    auto* Sequence=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle"));
    if(!TestNotNull(TEXT("Animation fixture"),Sequence))return false;
    F.Definitions[0]->EmotePresentation=UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence,TEXT("DefaultSlot"));
    FRPGRidingPresentationTestAccess::Mesh(*F.Visual)->InitAnim(true);
    ++F.State.Key.ActionSequence;F.State.Action=ERPGRidingAction::Emote;F.State.ExecutionId=FGuid::NewGuid();F.State.ActionDuration=3;F.Set();
    auto* Owned=FRPGRidingPresentationTestAccess::Montage(*F.Visual);auto* Model=FRPGRidingPresentationTestAccess::Mesh(*F.Visual);
    if(!TestTrue(TEXT("Owned montage started"),Owned&&Model&&FRPGRidingPresentationTestAccess::ActionId(*F.Visual)!=INDEX_NONE))return false;
    auto* Anim=Model->GetAnimInstance();const auto OldId=FRPGRidingPresentationTestAccess::ActionId(*F.Visual);
    Anim->Montage_Play(Owned,1.f,EMontagePlayReturnType::MontageLength,0.f,false);
    const auto* Foreign=Anim->GetActiveInstanceForMontage(Owned);const auto ForeignId=Foreign?Foreign->GetInstanceID():INDEX_NONE;
    TestTrue(TEXT("Another playback has a distinct instance"),ForeignId!=INDEX_NONE&&ForeignId!=OldId);
    F.State.Action=ERPGRidingAction::None;F.State.ExecutionId.Invalidate();F.State.ActionDuration=0;++F.State.Key.ActionSequence;F.Set();
    TestTrue(TEXT("Ending Emote preserves a newer playback of the same asset"),Anim->GetMontageInstanceForID(ForeignId)&&Anim->Montage_IsPlaying(Owned));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualEmoteRootMotion, "ProjectRPG.Riding.Presentation.EmoteRootMotion", Flags)
bool FRidingVisualEmoteRootMotion::RunTest(const FString&)
{
    FVisualFixture F;F.Complete();
    auto* Sequence=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/GreatswordComboPack/Animations/RootMotion/Greatsword_Combo5_1_rm.Greatsword_Combo5_1_rm"));
    if(!TestNotNull(TEXT("Animation fixture"),Sequence))return false;
    // Only this isolated process's loaded fixture changes; the flag is restored and no asset is saved.
    TGuardValue<bool> RootMotion(Sequence->bEnableRootMotion,true);
    F.Definitions[0]->EmotePresentation=UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence,TEXT("DefaultSlot"));
    if(!TestTrue(TEXT("Authored fixture really contains root motion"),F.Definitions[0]->EmotePresentation.Get()->HasRootMotion()))return false;
    ++F.State.Key.ActionSequence;F.State.Action=ERPGRidingAction::Emote;F.State.ExecutionId=FGuid::NewGuid();F.State.ActionDuration=3;F.Set();
    TestTrue(TEXT("Root-motion authored montage cannot be installed as Emote"),!FRPGRidingPresentationTestAccess::Montage(*F.Visual));
    TestTrue(TEXT("Cosmetic rejection preserves mount state and transform"),F.Riding->GetRidingState().Action==ERPGRidingAction::Emote&&F.Character->GetActorTransform().Equals(F.InitialActor));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualDash, "ProjectRPG.Riding.Presentation.DashPlayback", Flags)
bool FRidingVisualDash::RunTest(const FString&)
{
    FVisualFixture F; F.Complete();
    auto* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle"));
    if (!TestNotNull(TEXT("Read-only animation fixture"), Sequence)) return false;
    auto* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"));
    Montage->RateScale = 1.5f;
    auto* Model = FRPGRidingPresentationTestAccess::Mesh(*F.Visual); Model->InitAnim(true);
    F.Definitions[0]->DashPresentation = Montage;
    ++F.State.Key.ActionSequence; F.State.Action = ERPGRidingAction::Dash; F.State.ExecutionId = FGuid::NewGuid();
    F.State.ActionDuration = 0.6f; F.State.ActionDirection = FVector::ForwardVector;
    F.State.ActionStartServerWorldSeconds = F.World->GetTimeSeconds(); F.Set();
    const auto Id = FRPGRidingPresentationTestAccess::ActionId(*F.Visual);
    auto* Instance = Model->GetAnimInstance()->GetMontageInstanceForID(Id);
    if (!TestTrue(TEXT("Dash selects its own definition montage"), FRPGRidingPresentationTestAccess::Montage(*F.Visual) == Montage && Instance)) return false;
    TestTrue(TEXT("Dash duration accounts for authored montage rate scale"),
        FMath::IsNearlyEqual(Instance->GetPlayRate() * Montage->RateScale, Montage->GetPlayLength() / F.State.ActionDuration, 0.0001f));
    F.Set(); TestEqual(TEXT("Duplicate Dash observation does not restart playback"), FRPGRidingPresentationTestAccess::ActionId(*F.Visual), Id);
    const auto Token = FRPGRidingPresentationTestAccess::ActionToken(*F.Visual);
    F.State.Action = ERPGRidingAction::None; F.State.ExecutionId.Invalidate(); F.State.ActionDuration = 0;
    F.State.ActionStartServerWorldSeconds = 0; F.State.ActionDirection = FVector::ZeroVector; ++F.State.Key.ActionSequence; F.Set();
    FRPGRidingPresentationTestAccess::CompleteAction(*F.Visual, Token);
    TestTrue(TEXT("Ended Dash cannot be revived by an old callback"), !FRPGRidingPresentationTestAccess::Montage(*F.Visual)
        && !Model->GetAnimInstance()->Montage_IsPlaying(Montage));
    TestTrue(TEXT("Dash presentation preserves character and mesh transforms"), F.Character->GetActorTransform().Equals(F.InitialActor)
        && F.Character->GetMesh()->GetRelativeTransform().Equals(F.InitialMesh));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualDashTransitions, "ProjectRPG.Riding.Presentation.DashTransitions", Flags)
bool FRidingVisualDashTransitions::RunTest(const FString&)
{
    FVisualFixture F; F.Complete();
    auto* Sequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle"));
    if (!TestNotNull(TEXT("Animation fixture"), Sequence)) return false;
    auto* Emote = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"));
    auto* Dash = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, TEXT("DefaultSlot"));
    F.Definitions[0]->EmotePresentation = Emote; F.Definitions[0]->DashPresentation = Dash;
    auto* Model = FRPGRidingPresentationTestAccess::Mesh(*F.Visual); Model->InitAnim(true); auto* Anim = Model->GetAnimInstance();
    ++F.State.Key.ActionSequence; F.State.Action = ERPGRidingAction::Emote; F.State.ExecutionId = FGuid::NewGuid(); F.State.ActionDuration = 3; F.Set();
    const auto OldEmote = FRPGRidingPresentationTestAccess::ActionToken(*F.Visual);
    ++F.State.Key.ActionSequence; F.State.Action = ERPGRidingAction::Dash; F.State.ExecutionId = FGuid::NewGuid();
    F.State.ActionDuration = 0.6f; F.State.ActionDirection = FVector::ForwardVector; F.Set();
    const auto DashId = FRPGRidingPresentationTestAccess::ActionId(*F.Visual);
    TestTrue(TEXT("Dash preempts the private Emote playback"), FRPGRidingPresentationTestAccess::Montage(*F.Visual) == Dash
        && !Anim->Montage_IsPlaying(Emote) && Anim->GetMontageInstanceForID(DashId));
    FRPGRidingPresentationTestAccess::CompleteAction(*F.Visual, OldEmote);
    TestEqual(TEXT("Old Emote callback preserves current Dash"), FRPGRidingPresentationTestAccess::ActionId(*F.Visual), DashId);
    // A later caller using the same asset owns a different instance.
    Anim->Montage_Play(Dash, 1.f, EMontagePlayReturnType::MontageLength, 0.f, false);
    const auto* Foreign = Anim->GetActiveInstanceForMontage(Dash); const int32 ForeignId = Foreign ? Foreign->GetInstanceID() : INDEX_NONE;
    F.State.Action = ERPGRidingAction::None; F.State.ExecutionId.Invalidate(); F.State.ActionDuration = 0;
    F.State.ActionDirection = FVector::ZeroVector; ++F.State.Key.ActionSequence; F.Set();
    TestTrue(TEXT("Ending Dash preserves a foreign instance of the same montage"), ForeignId != INDEX_NONE && ForeignId != DashId
        && Anim->GetMontageInstanceForID(ForeignId) && Anim->Montage_IsPlaying(Dash));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingVisualDashRejection, "ProjectRPG.Riding.Presentation.DashRejection", Flags)
bool FRidingVisualDashRejection::RunTest(const FString&)
{
    FVisualFixture F; F.Complete();
    auto* RootSequence = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/GreatswordComboPack/Animations/RootMotion/Greatsword_Combo5_1_rm.Greatsword_Combo5_1_rm"));
    if (!TestNotNull(TEXT("Root motion fixture"), RootSequence)) return false;
    TGuardValue<bool> RestoreRootFlag(RootSequence->bEnableRootMotion, true);
    auto* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(RootSequence, TEXT("DefaultSlot"));
    if (!TestTrue(TEXT("Fixture really has root motion"), Montage && Montage->HasRootMotion())) return false;
    F.Definitions[0]->DashPresentation = Montage;
    FRPGRidingPresentationTestAccess::Mesh(*F.Visual)->InitAnim(true);
    ++F.State.Key.ActionSequence; F.State.Action = ERPGRidingAction::Dash; F.State.ExecutionId = FGuid::NewGuid();
    F.State.ActionDuration = 0.6f; F.State.ActionDirection = FVector::ForwardVector; F.Set();
    TestNull(TEXT("Root-motion Dash visual is rejected"), FRPGRidingPresentationTestAccess::Montage(*F.Visual));
    auto* Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/GreatswordComboPack/Demo/Mannequin/Animations/ThirdPersonIdle.ThirdPersonIdle"));
    if (!TestNotNull(TEXT("Idle fixture"), Idle)) return false;
    Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Idle, TEXT("DefaultSlot")); Montage->RateScale = 0;
    F.Definitions[0]->DashPresentation = Montage; ++F.State.Key.ActionSequence; F.State.ExecutionId = FGuid::NewGuid(); F.Set();
    TestNull(TEXT("Invalid authored rate is rejected"), FRPGRidingPresentationTestAccess::Montage(*F.Visual));
    TestTrue(TEXT("Cosmetic rejection leaves canonical server Dash unchanged"), RPGRiding::IsCanonical(F.Riding->GetRidingState())
        && F.Riding->GetRidingState().Action == ERPGRidingAction::Dash && F.Character->GetActorTransform().Equals(F.InitialActor));
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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRiderPoseTransform, "ProjectRPG.Riding.Presentation.RiderPoseTransform", Flags)
bool FRidingRiderPoseTransform::RunTest(const FString&)
{
    FVisualFixture F;
    auto* Rider = F.Character->GetMesh();
    Rider->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, MeshPath));
    F.Character->SetActorLocationAndRotation(FVector(350, -120, 50), FRotator(0, 75, 0));
    Rider->SetRelativeTransform(FTransform(FRotator(0, -90, 0), FVector(5, 0, -80), FVector(1.25)));
    F.Complete();
    const auto ActorBefore = F.Character->GetActorTransform();
    const auto MeshBefore = Rider->GetRelativeTransform();
    const auto View = F.Visual->GetPoseView();
    const auto Frame = RPGRidingAnim::Capture(Rider);
    TestTrue(TEXT("Current saddle is captured for the actual primary rider mesh"), Frame.bHasAnchor);
    TestTrue(TEXT("Component-space anchor reconstructs the full world saddle including offset, rotation and scale"),
        (Frame.RiderComponentTransform * Rider->GetComponentTransform()).Equals(View.RiderWorldTransform, 0.001));
    TestTrue(TEXT("Pose identity and class are captured together"), Frame.Key.RiderEpoch == F.State.Key.RiderEpoch
        && Frame.Key.StateRevision == F.State.Key.StateRevision && Frame.PoseLayer == View.PoseLayer);
    TestTrue(TEXT("Capture leaves actor and mesh transforms untouched"), F.Character->GetActorTransform().Equals(ActorBefore)
        && Rider->GetRelativeTransform().Equals(MeshBefore));
    TestEqual(TEXT("Capture leaves capsule radius untouched"), F.Character->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), F.InitialRadius);
    TestEqual(TEXT("Capture leaves capsule height untouched"), F.Character->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(), F.InitialHeight);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRiderPoseLifecycle, "ProjectRPG.Riding.Presentation.RiderPoseLifecycle", Flags)
bool FRidingRiderPoseLifecycle::RunTest(const FString&)
{
    FVisualFixture F;
    auto* Rider = F.Character->GetMesh();
    Rider->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, MeshPath));
    TestFalse(TEXT("Loading cannot publish an anchor"), RPGRidingAnim::Capture(Rider).bHasAnchor);
    F.Complete();
    const auto Captured = RPGRidingAnim::Capture(Rider);
    TestTrue(TEXT("Ready frame captured"), Captured.bHasAnchor);
    auto* Auxiliary = NewObject<USkeletalMeshComponent>(F.Character);
    Auxiliary->SetupAttachment(F.Character->GetRootComponent()); Auxiliary->RegisterComponent();
    Auxiliary->SetSkeletalMeshAsset(Rider->GetSkeletalMeshAsset());
    TestFalse(TEXT("Auxiliary meshes cannot consume the character's saddle"), RPGRidingAnim::Capture(Auxiliary).bHasAnchor);
    TestFalse(TEXT("Worker-thread capture fails before accessing live objects"),
        Async(EAsyncExecution::ThreadPool, [Rider] { return RPGRidingAnim::Capture(Rider).bHasAnchor; }).Get());
    F.State.Mode = ERPGRidingMode::OnFoot; F.State.MountId.Reset(); F.State.DefinitionVersion = 0;
    F.State.Movement = {}; ++F.State.Key.StateRevision; F.Set();
    const auto Cleared = RPGRidingAnim::Capture(Rider);
    TestTrue(TEXT("Dismount clears class, transform and identity together"), !Cleared.bHasAnchor && !Cleared.PoseLayer
        && !Cleared.Key.RiderEpoch.IsValid() && Cleared.RiderComponentTransform.Equals(FTransform::Identity));
    TestTrue(TEXT("Evaluation consumes its captured values, not a later live state"),
        Async(EAsyncExecution::ThreadPool, [Captured] { return Captured.bHasAnchor && Captured.Key.RiderEpoch.IsValid(); }).Get());
    F.State.Mode = ERPGRidingMode::Mounted; F.State.MountId = TEXT("wolf"); F.State.DefinitionVersion = 1;
    F.State.Movement = {1, 600, 2048, 2000, 500}; ++F.State.Key.StateRevision; F.Set(); F.Complete();
    const auto Replacement = RPGRidingAnim::Capture(Rider);
    TestTrue(TEXT("Replacement publishes only the new revision"), Replacement.bHasAnchor
        && Replacement.Key.StateRevision == F.State.Key.StateRevision && Replacement.Key.StateRevision != Captured.Key.StateRevision);
    F.Visual->UnregisterComponent();
    TestFalse(TEXT("Presentation teardown clears the next frame"), RPGRidingAnim::Capture(Rider).bHasAnchor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRiderPoseInvalidFrame, "ProjectRPG.Riding.Presentation.RiderPoseInvalidFrame", Flags)
bool FRidingRiderPoseInvalidFrame::RunTest(const FString&)
{
    FRPGRidingPoseView View; View.bHasAnchor = true; View.Key = {FGuid::NewGuid(), 1, 0};
    View.PoseLayer = UAnimInstance::StaticClass(); View.RiderWorldTransform.SetTranslation(FVector(0, 0, 40));
    const auto Rejected = [&](const FRPGRidingPoseView& V, const FTransform& Mesh)
    {
        const auto Frame = RPGRidingAnim::MakeFrame(V, Mesh);
        return !Frame.bHasAnchor && !Frame.PoseLayer && !Frame.Key.RiderEpoch.IsValid()
            && Frame.RiderComponentTransform.Equals(FTransform::Identity);
    };
    TestTrue(TEXT("Valid values convert"), RPGRidingAnim::MakeFrame(View, FTransform::Identity).bHasAnchor);
    auto Invalid = View; Invalid.bHasAnchor = false;
    TestTrue(TEXT("Missing anchor resets whole frame"), Rejected(Invalid, FTransform::Identity));
    Invalid = View; Invalid.Key.RiderEpoch.Invalidate();
    TestTrue(TEXT("Missing epoch rejected"), Rejected(Invalid, FTransform::Identity));
    Invalid = View; Invalid.Key.StateRevision = 0;
    TestTrue(TEXT("Missing revision rejected"), Rejected(Invalid, FTransform::Identity));
    Invalid = View; Invalid.PoseLayer = nullptr;
    TestTrue(TEXT("Missing pose class rejected"), Rejected(Invalid, FTransform::Identity));
    FTransform Mesh; Mesh.SetScale3D(FVector(0, 1, 1));
    TestTrue(TEXT("Singular mesh transform rejected before inversion"), Rejected(View, Mesh));
    Mesh.SetScale3D(FVector(-1, 1, 1));
    TestTrue(TEXT("Mirrored mesh transform rejected"), Rejected(View, Mesh));
    Invalid = View; Invalid.RiderWorldTransform.SetScale3D(FVector(0));
    TestTrue(TEXT("Singular saddle transform rejected"), Rejected(Invalid, FTransform::Identity));
    TestFalse(TEXT("No component has no frame"), RPGRidingAnim::Capture(nullptr).bHasAnchor);
    return true;
}
#endif
