#include "Riding/RPGRidingPresentationComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGMountDefinition.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/ScopeExit.h"

URPGRidingPresentationComponent::URPGRidingPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
    PlaceholderMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
}

bool URPGRidingPresentationComponent::CanPresent() const
{
    return !bEnding && IsValid(GetOwner()) && !GetOwner()->IsActorBeingDestroyed()
        && IsValid(GetWorld()) && !GetWorld()->bIsTearingDown && GetNetMode() != NM_DedicatedServer;
}

void URPGRidingPresentationComponent::BeginPlay() { Super::BeginPlay(); Refresh(); }
void URPGRidingPresentationComponent::OnRegister() { Super::OnRegister(); bEnding = false; if (HasBegunPlay()) Refresh(); }
void URPGRidingPresentationComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Tick, Function);
    Refresh();
}

void URPGRidingPresentationComponent::Detach()
{
    if (auto* Previous = Riding.Get()) Previous->OnChanged.Remove(ChangedHandle);
    ChangedHandle.Reset(); Riding.Reset(); BoundWorld.Reset();
    ClearVisuals();
}

void URPGRidingPresentationComponent::ClearVisuals()
{
    if (bPlayingEmote) { bClearPending = bRefreshPending = true; return; }
    // Invalidate first: canceling a handle must not resurrect a replaced visual through a queued delegate.
    LoadToken.Invalidate();
    if (LoadHandle) LoadHandle->CancelHandle();
    if (PlaceholderHandle) PlaceholderHandle->CancelHandle();
    LoadHandle.Reset(); PlaceholderHandle.Reset();
    if (bInstalling) { bClearPending = true; Status = ERPGRidingVisualStatus::Hidden; return; }
    auto* PreviousMount = MountMesh.Get();
    auto* PreviousPlaceholder = Placeholder.Get();
    auto* PreviousRoot = VisualRoot.Get();
    // Empty the owned handles before destruction can invoke an animation cleanup callback.
    LoadedPoseLayer = nullptr;
    MountMesh = nullptr; Placeholder = nullptr; VisualRoot = nullptr; RiderMesh.Reset();
    Requested = {}; MeshPath.Reset(); AnimPath.Reset(); PosePath.Reset();
    Status = ERPGRidingVisualStatus::Hidden;
    StopEmote();
    if (PreviousMount) PreviousMount->DestroyComponent();
    if (PreviousPlaceholder) PreviousPlaceholder->DestroyComponent();
    if (PreviousRoot) PreviousRoot->DestroyComponent();
}

void URPGRidingPresentationComponent::Refresh()
{
    if (bInstalling) { bRefreshPending = true; ClearVisuals(); return; }
    if (!CanPresent()) { Detach(); return; }
    auto* Character = Cast<ACharacter>(GetOwner());
    auto* Current = Character ? Character->FindComponentByClass<URPGRidingComponent>() : nullptr;
    if (Current != Riding || GetWorld() != BoundWorld)
    {
        Detach(); Riding = Current; BoundWorld = GetWorld();
        if (Current) ChangedHandle = Current->OnChanged.AddUObject(this, &ThisClass::HandleState);
    }
    if (!Current) return;
    const auto State = Current->GetRidingState();
    if (!RPGRiding::IsCanonical(State) || State.Mode != ERPGRidingMode::Mounted)
    { if (VisualRoot || LoadToken.IsValid()) ClearVisuals(); return; }
    const auto* Definition = Current->GetPresentationDefinition();
    const bool bSame = LoadToken.IsValid() && Requested.Key.RiderEpoch == State.Key.RiderEpoch
        && Requested.Key.StateRevision == State.Key.StateRevision && Requested.MountId == State.MountId
        && Requested.DefinitionVersion == State.DefinitionVersion && Character->GetMesh() == RiderMesh;
    if (bSame && ((!Definition && Status == ERPGRidingVisualStatus::Failed) || (Definition && MatchesDefinition(*Definition))))
    { RefreshEmote(); return; }
    ClearVisuals();
    Requested = State; LoadToken = FGuid::NewGuid();
    if (!EnsureRoot()) { ClearVisuals(); return; }
    Status = ERPGRidingVisualStatus::Loading;
    LoadPlaceholder();
    if (Definition) BeginLoad(*Definition);
    else { Status = ERPGRidingVisualStatus::Failed; UpdateVisibility(); }
}

void URPGRidingPresentationComponent::HandleState(const FRPGRidingRepState&) { Refresh(); }

bool URPGRidingPresentationComponent::EnsureRoot()
{
    auto* Character = Cast<ACharacter>(GetOwner());
    auto* Mesh = Character ? Character->GetMesh() : nullptr;
    if (!IsValid(Mesh) || !Mesh->IsRegistered()) return false;
    RiderMesh = Mesh;
    VisualRoot = NewObject<USceneComponent>(Character);
    VisualRoot->SetupAttachment(Mesh);
    VisualRoot->RegisterComponent();
    Placeholder = NewObject<UStaticMeshComponent>(Character);
    Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetGenerateOverlapEvents(false);
    Placeholder->SetCanEverAffectNavigation(false);
    Placeholder->SetupAttachment(VisualRoot);
    Placeholder->SetRelativeScale3D(FVector(1, 0.5, 0.5));
    Placeholder->RegisterComponent();
    return true;
}

void URPGRidingPresentationComponent::LoadPlaceholder()
{
    if (PlaceholderMesh.IsNull()) return;
    const auto Token = LoadToken;
    const TWeakObjectPtr<URPGRidingPresentationComponent> WeakThis(this);
    PlaceholderHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(PlaceholderMesh.ToSoftObjectPath(),
        FStreamableDelegate::CreateLambda([WeakThis, Token]()
        {
            auto* Self = WeakThis.Get();
            if (!Self || !Self->CanPresent() || Self->LoadToken != Token || !Self->Placeholder || Self->BoundWorld != Self->GetWorld()) return;
            Self->Placeholder->SetStaticMesh(Self->PlaceholderMesh.Get());
            Self->UpdateVisibility();
        }));
}

bool URPGRidingPresentationComponent::MatchesDefinition(const URPGMountDefinition& Definition) const
{
    return Definition.Mesh.ToSoftObjectPath() == MeshPath && Definition.AnimClass.ToSoftObjectPath() == AnimPath
        && Definition.RiderPoseLayer.ToSoftObjectPath() == PosePath && Definition.SaddleSocket == SaddleSocket
        && Definition.MountOffset.Equals(MountOffset) && Definition.RiderOffset.Equals(RiderOffset);
}

void URPGRidingPresentationComponent::BeginLoad(const URPGMountDefinition& Definition)
{
    MeshPath = Definition.Mesh.ToSoftObjectPath(); AnimPath = Definition.AnimClass.ToSoftObjectPath();
    PosePath = Definition.RiderPoseLayer.ToSoftObjectPath(); SaddleSocket = Definition.SaddleSocket;
    MountOffset = Definition.MountOffset; RiderOffset = Definition.RiderOffset;
    VisualRoot->SetRelativeTransform(MountOffset);
    const auto Token = LoadToken;
    const TWeakObjectPtr<URPGRidingPresentationComponent> WeakThis(this);
    LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(TArray<FSoftObjectPath>{MeshPath, AnimPath, PosePath},
        FStreamableDelegate::CreateLambda([WeakThis, Token]() { if (auto* Self = WeakThis.Get()) Self->CompleteLoad(Token); }));
    if (!LoadHandle) { Status = ERPGRidingVisualStatus::Failed; UpdateVisibility(); }
}

bool URPGRidingPresentationComponent::MatchesCurrent(const FGuid& Token) const
{
    const auto* Current = Riding.Get();
    const auto* Character = Cast<ACharacter>(GetOwner());
    if (!CanPresent() || !Token.IsValid() || LoadToken != Token || !Current || Current->GetOwner() != GetOwner()
        || BoundWorld != GetWorld() || !VisualRoot || !Character || RiderMesh != Character->GetMesh()) return false;
    const auto State = Current->GetRidingState();
    const auto* Definition = Current->GetPresentationDefinition();
    return RPGRiding::IsCanonical(State) && State.Mode == ERPGRidingMode::Mounted && Definition
        && State.Key.RiderEpoch == Requested.Key.RiderEpoch && State.Key.StateRevision == Requested.Key.StateRevision
        && State.MountId == Requested.MountId && State.DefinitionVersion == Requested.DefinitionVersion && MatchesDefinition(*Definition);
}

void URPGRidingPresentationComponent::CompleteLoad(const FGuid& Token)
{
    if (Status != ERPGRidingVisualStatus::Loading || !MatchesCurrent(Token)) return;
    auto* Mesh = Cast<USkeletalMesh>(MeshPath.ResolveObject());
    auto* AnimClass = Cast<UClass>(AnimPath.ResolveObject());
    auto* PoseClass = Cast<UClass>(PosePath.ResolveObject());
    const auto* Blueprint = Cast<UAnimBlueprintGeneratedClass>(AnimClass);
    if (!Mesh || !Mesh->GetSkeleton() || !AnimClass || !AnimClass->IsChildOf(UAnimInstance::StaticClass())
        || !PoseClass || !PoseClass->IsChildOf(UAnimInstance::StaticClass())
        || (Blueprint && Blueprint->GetTargetSkeleton() != Mesh->GetSkeleton()))
    { Status = ERPGRidingVisualStatus::Failed; UpdateVisibility(); return; }
    auto* NewMount = NewObject<USkeletalMeshComponent>(GetOwner());
    MountMesh = NewMount;
    LoadedPoseLayer = PoseClass;
    bInstalling = true;
    ON_SCOPE_EXIT
    {
        bInstalling = false;
        const bool bRefresh = bRefreshPending;
        bRefreshPending = false;
        if (bClearPending) { bClearPending = false; ClearVisuals(); }
        if (bRefresh) Refresh();
    };
    const auto StillCurrent = [this, Token, NewMount]()
    {
        if (Status == ERPGRidingVisualStatus::Loading && MatchesCurrent(Token) && MountMesh == NewMount && IsValid(NewMount)) return true;
        // NativeInitializeAnimation may synchronously end or replace Riding. Release only after the engine's
        // registration/initialization stack has returned, rather than destroying its component mid-callback.
        bClearPending = true; bRefreshPending = true;
        return false;
    };
    NewMount->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewMount->SetGenerateOverlapEvents(false);
    NewMount->SetCanEverAffectNavigation(false);
    NewMount->SetupAttachment(VisualRoot);
    NewMount->SetSkeletalMeshAsset(Mesh);
    if (!StillCurrent()) return;
    NewMount->SetAnimationMode(EAnimationMode::AnimationBlueprint);
    if (!StillCurrent()) return;
    NewMount->SetAnimInstanceClass(AnimClass);
    if (!StillCurrent()) return;
    NewMount->RegisterComponent();
    if (!StillCurrent()) return;
    NewMount->SetSimulatePhysics(false);
    if (!StillCurrent()) return;
    if (!NewMount->DoesSocketExist(SaddleSocket))
    {
        MountMesh->DestroyComponent(); MountMesh = nullptr;
        Status = ERPGRidingVisualStatus::Failed; UpdateVisibility(); return;
    }
    // Pose data is an input for an authored Riding layer. Do not replace the weapon layer or reattach GetMesh().
    LoadedPoseLayer = PoseClass;
    Status = ERPGRidingVisualStatus::Ready;
    UpdateVisibility();
    RefreshEmote();
}

void URPGRidingPresentationComponent::StopEmote()
{
    const auto OldAnim = EmoteAnim; auto* Montage = EmoteMontage.Get(); const int32 InstanceId = EmoteInstanceId;
    EmoteToken.Invalidate(); EmoteState = {}; EmoteAnim.Reset(); EmoteMontage = nullptr; EmoteInstanceId = INDEX_NONE;
    auto Handle = MoveTemp(EmoteLoadHandle); if (Handle) Handle->CancelHandle();
    // A later user of the same montage asset owns a different instance. Never stop that playback.
    if (auto* Anim = OldAnim.Get(); Anim && Montage)
        if (auto* Current = Anim->GetMontageInstanceForID(InstanceId); Current && Current->Montage == Montage)
            Current->Stop(FAlphaBlend(0.f));
}

void URPGRidingPresentationComponent::RefreshEmote()
{
    if (bUpdatingEmote) return;
    TGuardValue<bool> Updating(bUpdatingEmote, true);
    const auto Read = [this]() { return Riding.IsValid() ? Riding->GetDashMovementState() : FRPGRidingRepState{}; };
    auto State = Read();
    if (Status != ERPGRidingVisualStatus::Ready || !MatchesCurrent(LoadToken) || !RPGRiding::IsCanonical(State)
        || State.Action != ERPGRidingAction::Emote) { StopEmote(); return; }
    const auto* Clock = GetWorld()->GetGameState(); const double Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now - State.ActionStartServerWorldSeconds >= State.ActionDuration) { StopEmote(); return; }
    if (EmoteToken.IsValid() && EmoteState.ExecutionId == State.ExecutionId
        && RPGRiding::Compare(EmoteState.Key, State.Key) == ERPGRidingStateOrder::Same) return;
    StopEmote(); State = Read(); // Montage end callbacks may change the binding or action.
    if (Status != ERPGRidingVisualStatus::Ready || !MatchesCurrent(LoadToken) || !RPGRiding::IsCanonical(State) || State.Action != ERPGRidingAction::Emote) return;
    const auto* Definition = Riding->GetPresentationDefinition();
    if (!Definition || Definition->EmotePresentation.IsNull()) return;
    EmoteState = State; EmoteToken = FGuid::NewGuid(); const auto Token = EmoteToken;
    if (Definition->EmotePresentation.Get()) { CompleteEmote(Token); return; }
    const TWeakObjectPtr<URPGRidingPresentationComponent> Weak(this);
    EmoteLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Definition->EmotePresentation.ToSoftObjectPath(),
        FStreamableDelegate::CreateLambda([Weak, Token] { if (auto* Self = Weak.Get()) Self->CompleteEmote(Token); }));
}

void URPGRidingPresentationComponent::CompleteEmote(FGuid Token)
{
    if (!Token.IsValid() || Token != EmoteToken || Status != ERPGRidingVisualStatus::Ready || !MatchesCurrent(LoadToken)) return;
    const auto State = Riding->GetDashMovementState();
    if (State.Action != ERPGRidingAction::Emote || State.ExecutionId != EmoteState.ExecutionId
        || RPGRiding::Compare(State.Key, EmoteState.Key) != ERPGRidingStateOrder::Same) return;
    const auto* Clock = GetWorld()->GetGameState(); const double Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    const double Elapsed = FMath::Max(0., Now - State.ActionStartServerWorldSeconds);
    if (!FMath::IsFinite(Now) || Elapsed >= State.ActionDuration) return;
    const auto* Definition = Riding->GetPresentationDefinition();
    auto* Montage = Definition ? Definition->EmotePresentation.Get() : nullptr;
    auto* Anim = MountMesh ? MountMesh->GetAnimInstance() : nullptr;
    const auto* Asset = MountMesh ? MountMesh->GetSkeletalMeshAsset() : nullptr;
    if (!Montage || !Anim || !Asset || Montage->GetSkeleton() != Asset->GetSkeleton()
        || Montage->HasRootMotion() || !FMath::IsFinite(Montage->GetPlayLength()) || Montage->GetPlayLength() <= 0) return;
    if (!FMath::IsFinite(Montage->RateScale) || Montage->RateScale <= 0) return;
    const float Rate = Montage->GetPlayLength() / (State.ActionDuration * Montage->RateScale);
    if (!FMath::IsFinite(Rate) || Rate <= 0) return;
    TSet<int32> PreviousInstances;
    for (const auto* Previous : Anim->MontageInstances) if (Previous) PreviousInstances.Add(Previous->GetInstanceID());
    EmoteAnim = Anim; EmoteMontage = Montage; bPlayingEmote = true;
    // Our instance is inserted before montage-start callbacks can create another. Preserve other groups.
    const float Played = Anim->Montage_Play(Montage, Rate, EMontagePlayReturnType::MontageLength,
        float(Elapsed * Rate * Montage->RateScale), false);
    FAnimMontageInstance* Instance = nullptr;
    if (IsValid(Anim)) for (auto* Candidate : Anim->MontageInstances)
        if (Candidate && Candidate->Montage == Montage && !PreviousInstances.Contains(Candidate->GetInstanceID())) { Instance = Candidate; break; }
    bPlayingEmote = false;
    if (Token == EmoteToken && EmoteAnim == Anim && EmoteMontage == Montage)
    {
        EmoteInstanceId = Played > 0 && Instance ? Instance->GetInstanceID() : INDEX_NONE;
        const auto Latest = Riding.IsValid() ? Riding->GetDashMovementState() : FRPGRidingRepState{};
        if (!MatchesCurrent(LoadToken) || Latest.Action != ERPGRidingAction::Emote || Latest.ExecutionId != EmoteState.ExecutionId
            || RPGRiding::Compare(Latest.Key, EmoteState.Key) != ERPGRidingStateOrder::Same) StopEmote();
    }
    else if (Played > 0 && Instance) Instance->Stop(FAlphaBlend(0.f));
    if (!bInstalling)
    {
        const bool RefreshPending = bRefreshPending; bRefreshPending = false;
        if (bClearPending) { bClearPending = false; ClearVisuals(); }
        if (RefreshPending) Refresh();
    }
}

void URPGRidingPresentationComponent::UpdateVisibility()
{
    if (Placeholder) Placeholder->SetVisibility(Status != ERPGRidingVisualStatus::Ready, true);
    if (MountMesh) MountMesh->SetVisibility(Status == ERPGRidingVisualStatus::Ready, true);
}

FRPGRidingPoseView URPGRidingPresentationComponent::GetPoseView() const
{
    FRPGRidingPoseView View;
    if (Status != ERPGRidingVisualStatus::Ready || !MountMesh || !MatchesCurrent(LoadToken)) return View;
    if (const auto* PoseBlueprint = Cast<UAnimBlueprintGeneratedClass>(LoadedPoseLayer.Get()))
    {
        const auto* RiderAsset = RiderMesh.IsValid() ? RiderMesh->GetSkeletalMeshAsset() : nullptr;
        if (!RiderAsset || PoseBlueprint->GetTargetSkeleton() != RiderAsset->GetSkeleton()) return View;
    }
    View.Key = Riding->GetRidingState().Key;
    View.PoseLayer = LoadedPoseLayer;
    View.RiderWorldTransform = RiderOffset * MountMesh->GetSocketTransform(SaddleSocket, RTS_World);
    View.bHasAnchor = View.RiderWorldTransform.IsValid();
    return View;
}

void URPGRidingPresentationComponent::RetryPresentation() { if (Status == ERPGRidingVisualStatus::Failed) { ClearVisuals(); Refresh(); } }
void URPGRidingPresentationComponent::EndPlay(const EEndPlayReason::Type Reason) { bEnding = true; Detach(); Super::EndPlay(Reason); }
void URPGRidingPresentationComponent::OnUnregister() { bEnding = true; Detach(); Super::OnUnregister(); }
