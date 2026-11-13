#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGRidingStatePolicy.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingWorldPolicy.h"
#include "Riding/RPGRidingAbilityGate.h"
#include "Riding/RPGRidingConsumeLease.h"
#include "Riding/RPGRidingInteractionScope.h"
#include "Riding/RPGRidingTimedInteractionLease.h"
#include "Riding/RPGRidingDashRuntime.h"
#include "Riding/RPGRidingDashPrediction.h"
#include "Riding/RPGGameplayAbility_RidingDash.h"
#include "Riding/RPGRidingDashRootMotion.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

URPGRidingComponent::URPGRidingComponent()
{
    DashRuntime.Reset(new FRPGRidingDashRuntime(*this));
    DashPrediction.Reset(new FRPGRidingDashPrediction(*this));
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
}
void FRPGRidingDashRuntimeDeleter::operator()(FRPGRidingDashRuntime* P) const { delete P; }
void FRPGRidingDashPredictionDeleter::operator()(FRPGRidingDashPrediction* P) const { delete P; }
URPGRidingComponent::~URPGRidingComponent() = default;

FRPGRidingRepState URPGRidingComponent::GetDashMovementState() const { return GetOwner() && GetOwner()->HasAuthority() ? State : DashPrediction->View(); }
bool URPGRidingComponent::IsDashMovementActive() const { return IsServerDashActive() || DashPrediction->HasMotion(); }
float URPGRidingComponent::GetPredictedDashCooldownRemaining() const { return DashPrediction->CooldownRemaining(); }
void URPGRidingComponent::PrepareDashPredictionMovement() { DashPrediction->Prepare(); }
void URPGRidingComponent::FinalizeDashPredictionMovement() { DashPrediction->Finalize(); }
double URPGRidingComponent::ConstrainPredictedDashMovement(const FVector& A, const FVector& B) { return DashPrediction->Constrain(A, B); }
void URPGRidingComponent::RecordPredictedDashMovement(FGuid Id, const FVector& A, const FVector& B) { DashPrediction->Record(Id, A, B); }
void URPGRidingComponent::OnRep_DashGrant() { DashPrediction->Update(); }

ERPGRidingResultCode URPGRidingComponent::StartServerDash(FRPGRidingStateKey Expected, FVector Direction, FRPGRidingDashActivation Activation)
{ return DashRuntime->Start(Expected, Direction, Activation); }
bool URPGRidingComponent::IsServerDashActive() const
{ return GetOwner() && GetOwner()->HasAuthority() && DashRuntime->HasMotion(); }
void URPGRidingComponent::PrepareServerDashMovement()
{ if (GetOwner() && GetOwner()->HasAuthority()) DashRuntime->PrepareMovement(); }
void URPGRidingComponent::FinalizeServerDashMovement()
{ if (GetOwner() && GetOwner()->HasAuthority()) DashRuntime->ClampAfterStateChange(); }
void URPGRidingComponent::RecordServerDashMovement(FGuid ExecutionId, const FVector& Start, const FVector& End)
{ if (GetOwner() && GetOwner()->HasAuthority() && ExecutionId.IsValid() && ExecutionId == ActiveDash.ExecutionId) DashRuntime->RecordStep(Start, End); }
void URPGRidingComponent::StopServerDashForControl()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    DashRuntime->Stop(ERPGRidingEndReason::ExecutionRejected);
    if (State.Action == ERPGRidingAction::Dash) FinishDash(ActiveDash, ERPGRidingEndReason::ExecutionRejected);
}
void URPGRidingComponent::BeginPlay()
{
    Super::BeginPlay();
    if (auto* Pawn = Cast<APawn>(GetOwner()))
    {
        Pawn->ReceiveControllerChangedDelegate.AddDynamic(this, &ThisClass::HandleControllerChanged);
        Pawn->ReceiveRestartedDelegate.AddDynamic(this, &ThisClass::HandleRestarted);
    }
    RefreshBinding();
}
void URPGRidingComponent::HandleControllerChanged(APawn*, AController*, AController*) { RefreshBinding(); }
void URPGRidingComponent::HandleRestarted(APawn*) { RefreshBinding(); }

bool URPGRidingComponent::IsBindingCurrent() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* S = Session.Get();
    return !bEnding && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->HasAuthority()
        && IsValid(S) && S->IsContextCurrent() && State.Key.RiderEpoch.IsValid()
        && S->BoundPawn == this && S->BoundEpoch == State.Key.RiderEpoch && Pawn->GetWorld() == S->GetWorld()
        && Controller.IsValid() && Pawn->GetController() == Controller && Controller->GetPawn() == Pawn
        && Pawn->GetPlayerState() == S->GetOwner();
}

void URPGRidingComponent::RefreshBinding()
{
    auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->HasAuthority() || bEnding) return;
    if (IsBindingCurrent()) return;
    ReleaseBinding();
    // Ability-removal callbacks can already establish a fresh binding.
    if (IsBindingCurrent() || bEnding || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()) return;
    auto* PS = Pawn->GetPlayerState();
    auto* S = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
    if (!S) return;
    const FGuid NewEpoch = S->Bind(*this);
    if (!NewEpoch.IsValid()) return;
    Session = S; Controller = Pawn->GetController();
    State = {}; State.Key = {NewEpoch, 1, 0};
    Publish();
}

void URPGRidingComponent::ReleaseBinding()
{
    const auto OldEmoteHandle = EmoteAbilityHandle;
    DashPrediction->Stop();
    DashRuntime->Stop(ERPGRidingEndReason::ExecutionRejected, false);
    ActiveDash = {}; DashDeadline = 0;
    ActiveEmote = {}; EmoteDeadline = 0;
    const FGuid OldEpoch = State.Key.RiderEpoch;
    auto* Previous = Session.Get();
    Session.Reset(); Controller.Reset();
    if (Previous) Previous->Unbind(*this, OldEpoch);
    const bool WasBound = GetOwner() && GetOwner()->HasAuthority() && OldEpoch.IsValid();
    if (WasBound)
    {
        State = {};
        DashRuntime->ClampAfterStateChange();
    }
    ReleaseDashAbility();
    if (EmoteAbilityHandle == OldEmoteHandle) ReleaseEmoteAbility();
    // A removal callback may have published a new epoch. The old release cannot clear it.
    if (WasBound && !State.Key.RiderEpoch.IsValid() && !Session.IsValid()) Publish();
}

void URPGRidingComponent::Publish()
{
    SyncDashAbility();
    SyncEmoteAbility();
    MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, State, this);
    if (GetOwner()) GetOwner()->ForceNetUpdate();
    const auto Snapshot = State;
    OnChanged.Broadcast(Snapshot);
}

void URPGRidingComponent::OnRep_State(const FRPGRidingRepState& Previous)
{
    if (!RPGRiding::IsCanonical(State)
        || (State.Key.RiderEpoch == Previous.Key.RiderEpoch && RPGRiding::Compare(State.Key, Previous.Key) == ERPGRidingStateOrder::Older))
    { State = Previous; return; }
    if (auto* C = Cast<ACharacter>(GetOwner()); C && C->GetLocalRole() == ROLE_AutonomousProxy)
    {
        DashPrediction->Update();
        RPGRidingDashMotion::Reconcile(*C, GetDashMovementState(), true);
    }
    const auto Snapshot = State;
    OnChanged.Broadcast(Snapshot);
}

void URPGRidingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ThisClass, State);
    DOREPLIFETIME_CONDITION(ThisClass, DashGrant, COND_OwnerOnly);
}

const URPGMountDefinition* URPGRidingComponent::FindDefinition(const FString& Id) const
{
    if (Definitions.Num() > RPGRiding::MaxMountDefinitions) return nullptr;
    const URPGMountDefinition* Found = nullptr;
    for (const URPGMountDefinition* Definition : Definitions)
    {
        if (!Definition || Definition->MountId != Id) continue;
        if (Found) return nullptr;
        TArray<FText> Errors;
        if (!Definition->ValidateMetadata(Errors)) return nullptr;
        Found = Definition;
    }
    return Found;
}

const URPGMountDefinition* URPGRidingComponent::GetPresentationDefinition() const
{
    if (!RPGRiding::IsCanonical(State) || State.Mode != ERPGRidingMode::Mounted) return nullptr;
    const auto* Definition = FindDefinition(State.MountId);
    return Definition && Definition->DefinitionVersion == State.DefinitionVersion ? Definition : nullptr;
}

bool URPGRidingComponent::TryBuildMovementProfile(FRPGRidingMovementProfile& Out) const
{
    Out = {};
    if (!RPGRiding::IsCanonical(State) || State.Mode != ERPGRidingMode::Mounted || bEnding) return false;
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        if (!IsBindingCurrent()) return false;
        FRPGRidingEvaluationContext Guards; BuildGuards(Guards);
        if (!Guards.bReady || !Guards.bAlive || Guards.bActionBlocked || Guards.bSlowActive || !Guards.bOnGround) return false;
        const auto* Definition = FindDefinition(State.MountId);
        if (!Definition || !IsRegionAllowed(*Definition)) return false;
    }
    Out = State.Movement;
    return true;
}

bool URPGRidingComponent::IsRegionAllowed(const URPGMountDefinition& Definition) const
{
    const auto* Character = Cast<ACharacter>(GetOwner());
    const auto* Policy = GetWorld() ? GetWorld()->GetSubsystem<URPGRidingWorldPolicy>() : nullptr;
    if (!Character || !Policy) return false;
    const FVector Location = Character->GetCapsuleComponent()->GetComponentLocation();
    return Policy->EvaluatePath(*Character, Location, Location, Definition.RegionTags).bAllowed;
}

bool URPGRidingComponent::ApplyMovementCorrection(const FRPGRidingRepState& Incoming)
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || Pawn->HasAuthority() || Pawn->GetLocalRole() != ROLE_AutonomousProxy
        || !RPGRiding::IsCanonical(Incoming) || Incoming.Key.RiderEpoch != State.Key.RiderEpoch
        || RPGRiding::Compare(Incoming.Key, State.Key) != ERPGRidingStateOrder::Newer) return false;
    // A response can advance an already-bound epoch, never install an unrelated binding or resurrect old state.
    const auto Previous = State; State = Incoming; OnRep_State(Previous);
    return true;
}

void URPGRidingComponent::BuildGuards(FRPGRidingEvaluationContext& Out) const
{
    Out.Current = State;
    Out.bHasAuthority = IsBindingCurrent();
    Out.bAuthorityRevoked = !Out.bHasAuthority;
    auto* Pawn = Cast<APawn>(GetOwner());
    auto* ASC = Pawn ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true) : nullptr;
    Out.bReady = ASC && ASC->GetAvatarActor() == Pawn && Session.IsValid()
        && Session->Runtime->GetStatus() == ERPGRidingSessionStatus::Ready;
    if (!ASC || ASC->GetAvatarActor() != Pawn) return;
    // Read the existing GAS health field without depending on a concrete game's AttributeSet class.
    for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
    {
        FProperty* Property = Set ? FindFProperty<FProperty>(Set->GetClass(), TEXT("CurrentHealth")) : nullptr;
        const FGameplayAttribute Health(Property);
        if (Health.IsValid())
        {
            const float Value = ASC->GetNumericAttribute(Health);
            Out.bAlive = FMath::IsFinite(Value) && Value > 0;
            break;
        }
    }
    const auto Has = [ASC](const TCHAR* Name)
    {
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
        return Tag.IsValid() && ASC->HasMatchingGameplayTag(Tag);
    };
    Out.bAlive &= !Has(TEXT("Shared.Status.Death"));
    Out.bActionBlocked = Has(TEXT("Player.Status.Holding")) || Has(TEXT("Shared.Status.Control.Stunned"))
        || Has(TEXT("Shared.Status.Control.Knockback")) || Has(TEXT("Shared.Status.Control.Airborne")) || Has(TEXT("Shared.Status.Control.Staggered"));
    Out.bSlowActive = Has(TEXT("Shared.Status.Movement.Slowed"));
    Out.bActionBlocked |= Has(TEXT("Player.Status.Rolling")) || Has(TEXT("Player.Status.Blocking"));
    Out.bActionBlocked |= FRPGRidingConsumeLease::HasPending(Pawn);
    Out.bActionBlocked |= FRPGRidingInteractionScope::HasPending(Pawn);
    Out.bActionBlocked |= FRPGRidingTimedInteractionLease::HasPending(Pawn);
    for (const auto& Spec : ASC->GetActivatableAbilities())
        if (Spec.IsActive() && FRPGRidingAbilityGate::BlocksMounting(Spec.Ability)) Out.bActionBlocked = true;
    const auto* Character = Cast<ACharacter>(Pawn);
    const auto* Move = Character ? Character->GetCharacterMovement() : nullptr;
    Out.bActionBlocked |= Character && Character->bIsCrouched;
    Out.bOnGround = Move && Move->MovementMode == MOVE_Walking;
    Out.bInCombat = true;
    if (const auto* S = Session.Get())
    {
        const auto Timing = S->Runtime->GetTiming(FPlatformTime::Seconds());
        Out.bInCombat = !Timing.bValid || Timing.CombatRemaining > 0;
    }
}

void URPGRidingComponent::RequestSetMounted(bool bMounted, const FRPGRidingSelector& Selector)
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->IsLocallyControlled() || !State.Key.RiderEpoch.IsValid()) return;
    if (LocalRequestEpoch != State.Key.RiderEpoch) { LocalRequestEpoch = State.Key.RiderEpoch; LocalRequestSeq = 0; }
    if (LocalRequestSeq == MAX_uint32) return;
    FRPGRidingSetCommand Command;
    Command.RiderEpoch = LocalRequestEpoch; Command.RequestSeq = ++LocalRequestSeq;
    Command.ExpectedStateRevision = State.Key.StateRevision; Command.bDesiredMounted = bMounted;
    if (bMounted) Command.Selector = Selector;
    ServerSetRiding(Command);
}

void URPGRidingComponent::ServerSetRiding_Implementation(FRPGRidingSetCommand Command)
{
    RefreshBinding();
    if (!IsBindingCurrent()) return;
    auto* S = Session.Get();
    if (S->SnapshotExpiresAt > 0 && FPlatformTime::Seconds() >= S->SnapshotExpiresAt)
    { S->ExpireAdmissionSnapshot(S->AuthorityContext); if (!IsBindingCurrent() || Session != S) return; }
    auto Start = S->Runtime->BeginCommand(Command, State, FPlatformTime::Seconds());
    if (Command.bDesiredMounted && Start.Disposition == ERPGRidingRequestDisposition::Rejected && Start.Receipt.Result == ERPGRidingResultCode::NotReady)
        S->RequestAdmissionSnapshot(true); // Only a fresh validated/budgeted mount command may request manual recovery.
    if (Start.Disposition == ERPGRidingRequestDisposition::Pending) return;
    if (Start.Disposition == ERPGRidingRequestDisposition::Reserved)
    {
        FRPGRidingEvaluationContext Context;
        BuildGuards(Context);
        Context.bActionBlocked |= S->PendingDamageScopes.Num() > 0;
        FRPGRidingAdmissionSnapshot Owned;
        if (Command.bDesiredMounted && S->Runtime->CopySnapshot(Owned))
        {
            FString Id = Command.Selector.Kind == ERPGRidingSelectorKind::Specific ? Command.Selector.MountId : Owned.SelectedMountId;
            if (Command.Selector.Kind == ERPGRidingSelectorKind::RandomFavorites)
            {
                TArray<FString> Eligible;
                for (const auto& Favorite : Owned.Favorites)
                    if (const auto* Definition = FindDefinition(Favorite); Definition && IsRegionAllowed(*Definition)) Eligible.Add(Favorite);
                Id = Eligible.IsEmpty() ? FString() : Eligible[FMath::RandHelper(Eligible.Num())];
            }
            Context.CandidateMountId = Id;
            Context.bCandidateOwned = Owned.OwnedMountIds.Contains(Id);
            if (const auto* Definition = FindDefinition(Id))
            {
                Context.CandidateDefinitionVersion = Definition->DefinitionVersion;
                Context.bDefinitionCompatible = true;
                Context.bRegionAllowed = IsRegionAllowed(*Definition);
            }
        }
        const auto Decision = RPGRidingStatePolicy::Evaluate(Context, Command);
        auto ResultCode = Decision.Result;
        if (Decision.Result == ERPGRidingResultCode::Applied && !Commit(Decision)) ResultCode = ERPGRidingResultCode::NotReady;
        if (!IsBindingCurrent()) ResultCode = ERPGRidingResultCode::AuthorityRevoked;
        else if (ResultCode == ERPGRidingResultCode::Applied && Command.bDesiredMounted && State.Mode != ERPGRidingMode::Mounted)
            ResultCode = ERPGRidingResultCode::ActionBlocked;
        Start.Receipt = {ResultCode, State};
        if (!S->Runtime->CompleteCommand(Start.Token, Start.Receipt)) return;
    }
    if (!IsBindingCurrent() || Session != S) return;
    S->PublishOwnerState();
    ClientRidingResult({Command.RequestSeq, Start.Receipt.Result, Start.Receipt.State, State});
}

void URPGRidingComponent::ClientRidingResult_Implementation(FRPGRidingCommandResult Result)
{
    // RPC results never install authority state or adopt an Epoch. RepNotify and newer same-epoch CMC corrections own state.
    if (Result.LatestState.Key.RiderEpoch != State.Key.RiderEpoch || !State.Key.RiderEpoch.IsValid()) return;
    OnResult.Broadcast(Result);
}

void URPGRidingComponent::MakeOnFoot(ERPGRidingEndReason Reason)
{
    DashRuntime->Stop(Reason, false);
    ActiveDash = {}; DashDeadline = 0;
    ActiveEmote = {}; EmoteDeadline = 0;
    const auto Key = State.Key;
    State = {};
    State.Key = Key;
    ++State.Key.StateRevision;
    State.LastEndReason = Reason;
    DashRuntime->ClampAfterStateChange();
}

bool URPGRidingComponent::Commit(const FRPGRidingDecision& Decision)
{
    if (Decision.Result != ERPGRidingResultCode::Applied || !IsBindingCurrent()) return false;
    if (State.Key.StateRevision >= MAX_uint32 - 1 || State.Key.ActionSequence >= MAX_uint32 - 1)
    { ReleaseBinding(); RefreshBinding(); return false; }
    FRPGRidingMovementProfile Profile;
    if (Decision.Transition != ERPGRidingTransition::Dismount && !Session->Runtime->CopyMovementProfile(Profile)) return false;
    TGuardValue<bool> Applying(bApplying, true);
    if (Decision.Transition == ERPGRidingTransition::Dismount) MakeOnFoot(ERPGRidingEndReason::Manual);
    else
    {
        ActiveEmote = {}; EmoteDeadline = 0;
        const auto Key = State.Key;
        State = {}; State.Key = Key; ++State.Key.StateRevision;
        State.Mode = ERPGRidingMode::Mounted;
        State.MountId = Decision.ResolvedMountId; State.DefinitionVersion = Decision.DefinitionVersion;
        State.Movement = Profile;
    }
    Publish();
    if (PendingEnd != ERPGRidingEndReason::None && IsBindingCurrent())
    {
        const auto Reason = PendingEnd; PendingEnd = ERPGRidingEndReason::None;
        if (State.Mode == ERPGRidingMode::Mounted) { MakeOnFoot(Reason); Publish(); }
    }
    PendingEnd = ERPGRidingEndReason::None;
    return true;
}

void URPGRidingComponent::ForceDismount(ERPGRidingEndReason Reason)
{
    if (!IsBindingCurrent() || Reason == ERPGRidingEndReason::None
        || static_cast<uint8>(Reason) > static_cast<uint8>(ERPGRidingEndReason::Completed)) return;
    if (bApplying) { if (PendingEnd == ERPGRidingEndReason::None || Reason == ERPGRidingEndReason::AuthorityRevoked || Reason == ERPGRidingEndReason::Death) PendingEnd = Reason; return; }
    if (State.Mode != ERPGRidingMode::Mounted) return;
    if (State.Key.StateRevision >= MAX_uint32 - 1) { ReleaseBinding(); RefreshBinding(); return; }
    MakeOnFoot(Reason); Publish();
}

void URPGRidingComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Tick, Function);
    ValidateServerMovementContext();
    SyncDashAbility();
    SyncEmoteAbility();
    DashPrediction->Update();
    DashRuntime->Tick();
    if (State.Action == ERPGRidingAction::Dash && FPlatformTime::Seconds() >= DashDeadline)
        FinishDash(ActiveDash, ERPGRidingEndReason::Completed);
}

bool URPGRidingComponent::OwnsDashAbility(const UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle) const
{
    return ASC && IsBindingCurrent() && State.Mode == ERPGRidingMode::Mounted
        && DashAbilityOwner.Get() == ASC && Handle.IsValid() && DashAbilityHandle == Handle
        && DashGrantKey.RiderEpoch == State.Key.RiderEpoch && DashGrantKey.StateRevision == State.Key.StateRevision
        && DashGrant.Matches(State) && DashGrant.AbilityHandle == Handle
        && ASC->GetAvatarActor() == GetOwner();
}

void URPGRidingComponent::ReleaseDashAbility()
{
    auto* ASC = DashAbilityOwner.Get();
    const auto Handle = DashAbilityHandle;
    DashAbilityOwner.Reset(); DashAbilityHandle = {}; DashGrantKey = {};
    const bool HadGrant = !DashGrant.IsEmpty();
    DashGrant = {};
    if (HadGrant && GetOwner() && GetOwner()->HasAuthority())
    {
        MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, DashGrant, this);
        GetOwner()->ForceNetUpdate();
    }
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    if (Spec && Spec->SourceObject.Get() == this && Spec->Ability
        && Spec->Ability->GetClass() == URPGGameplayAbility_RidingDash::StaticClass()) ASC->ClearAbility(Handle);
}

void URPGRidingComponent::SyncDashAbility()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    if (!GetOwner() || !GetOwner()->HasAuthority() || bSyncingDashAbility) return;
    TGuardValue<bool> Syncing(bSyncingDashAbility, true);
    const auto ReadyASC = [this]() -> UAbilitySystemComponent*
    {
        if (!IsRegistered() || !IsBindingCurrent() || State.Mode != ERPGRidingMode::Mounted
            || Session->Runtime->GetStatus() != ERPGRidingSessionStatus::Ready) return nullptr;
        FRPGRidingMovementProfile Movement;
        if (!Session->Runtime->CopyMovementProfile(Movement) || Movement.Version != State.Movement.Version) return nullptr;
        auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), true);
        return IsValid(ASC) && ASC->GetAvatarActor() == GetOwner() && ASC->GetWorld() == GetWorld() ? ASC : nullptr;
    };
    auto* ASC = ReadyASC();
    const auto* Current = ASC ? ASC->FindAbilitySpecFromHandle(DashAbilityHandle) : nullptr;
    if (OwnsDashAbility(ASC, DashAbilityHandle) && Current && !Current->PendingRemove && !Current->RemoveAfterActivation
        && Current->SourceObject.Get() == this && Current->Ability
        && Current->Ability->GetClass() == URPGGameplayAbility_RidingDash::StaticClass())
    {
        FRPGRidingDashProfile P;
        if (Session->Runtime->CopyDashProfile(P) && P.Distance == DashGrant.Distance && P.Duration == DashGrant.Duration && P.Cooldown == DashGrant.Cooldown) return;
    }
    ReleaseDashAbility();
    // Removal can execute arbitrary ability-end delegates and change the binding.
    ASC = ReadyASC();
    if (!ASC) return;
    FRPGRidingDashProfile Profile;
    if (!Session->Runtime->CopyDashProfile(Profile)) return;
    FGameplayAbilitySpec Spec(URPGGameplayAbility_RidingDash::StaticClass(), 1, INDEX_NONE, this);
    DashAbilityOwner = ASC; DashAbilityHandle = Spec.Handle; DashGrantKey = State.Key;
    DashGrant.GrantId = FGuid::NewGuid(); DashGrant.RiderEpoch = State.Key.RiderEpoch;
    DashGrant.MountRevision = State.Key.StateRevision; DashGrant.AbilityHandle = Spec.Handle;
    DashGrant.CatalogVersion = State.Movement.Version; DashGrant.Distance = Profile.Distance;
    DashGrant.Duration = Profile.Duration; DashGrant.Cooldown = Profile.Cooldown;
    if (!DashGrant.IsValid()) { ReleaseDashAbility(); return; }
    MARK_PROPERTY_DIRTY_FROM_NAME(ThisClass, DashGrant, this);
    GetOwner()->ForceNetUpdate();
    const auto GrantedHandle = Spec.Handle;
    ASC->GiveAbility(Spec);
    if (ReadyASC() != ASC || !OwnsDashAbility(ASC, GrantedHandle)) ReleaseDashAbility();
}

bool URPGRidingComponent::TryGetDashGrant(FRPGRidingDashGrant& Out) const
{
    Out = {};
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(PC)
        || PC->GetPawn() != Pawn || PC->GetWorld() != GetWorld() || !DashGrant.Matches(State)) return false;
    UAbilitySystemComponent* ASC;
    const auto Handle = FindDashAbility(ASC);
    const auto* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    if (!Spec || Handle != DashGrant.AbilityHandle) return false;
    if (Pawn->HasAuthority())
    {
        FRPGRidingDashProfile P;
        if (!OwnsDashAbility(ASC, Handle) || !Session->Runtime->CopyDashProfile(P)
            || P.Distance != DashGrant.Distance || P.Duration != DashGrant.Duration || P.Cooldown != DashGrant.Cooldown) return false;
    }
    else if (Pawn->GetLocalRole() != ROLE_AutonomousProxy || !Pawn->IsLocallyControlled() || !PC->IsLocalController()) return false;
    Out = DashGrant;
    return true;
}

bool URPGRidingComponent::IsDashActivationCurrent(const FRPGRidingDashActivation& Activation) const
{
    FRPGRidingDashGrant Current;
    if (!Activation.IsValid() || !TryGetDashGrant(Current) || !Current.SameGrant(Activation.Grant)) return false;
    UAbilitySystemComponent* ASC;
    const auto Handle = FindDashAbility(ASC);
    if (Handle != Current.AbilityHandle) return false;
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Current.AbilityHandle) : nullptr;
    const auto* Ability = Spec ? Spec->GetPrimaryInstance() : nullptr;
    return Spec && Spec->IsActive() && Ability && Ability->IsActive()
        && Ability->GetCurrentActivationInfo().GetActivationPredictionKey() == Activation.PredictionKey;
}

FGameplayAbilitySpecHandle URPGRidingComponent::FindDashAbility(UAbilitySystemComponent*& OutASC) const
{
    OutASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), true);
    if (!IsValid(OutASC) || OutASC->GetAvatarActor() != GetOwner() || OutASC->GetWorld() != GetWorld()) return {};
    FGameplayAbilitySpecHandle Found;
    for (const auto& Spec : OutASC->GetActivatableAbilities())
    {
        if (Spec.SourceObject.Get() != this || !Spec.Ability
            || Spec.Ability->GetClass() != URPGGameplayAbility_RidingDash::StaticClass() || Spec.PendingRemove || Spec.RemoveAfterActivation) continue;
        if (Found.IsValid()) return {}; // Ambiguous replicated grants fail closed.
        Found = Spec.Handle;
    }
    return Found;
}

bool URPGRidingComponent::CanRequestDash() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(PC)
        || PC->GetPawn() != Pawn || PC->GetWorld() != GetWorld() || !RPGRiding::IsCanonical(State)
        || State.Mode != ERPGRidingMode::Mounted
        || (State.Action != ERPGRidingAction::None && State.Action != ERPGRidingAction::Emote)) return false;
    UAbilitySystemComponent* ASC;
    const auto Handle = FindDashAbility(ASC);
    const auto* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    if (!Spec || Spec->IsActive()) return false;
    FRPGRidingDashGrant Approved;
    if (!TryGetDashGrant(Approved) || Approved.AbilityHandle != Handle) return false;
    if (Pawn->HasAuthority()) return OwnsDashAbility(ASC, Handle) && EvaluateDashAdmission(State.Key) == ERPGRidingResultCode::Applied;
    if (!Pawn->IsLocallyControlled() || Pawn->GetLocalRole() != ROLE_AutonomousProxy || !PC->IsLocalController()) return false;
    return IsDashOwnerTimingReady();
}

bool URPGRidingComponent::IsDashOwnerTimingReady() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* PS = Pawn ? Pawn->GetPlayerState() : nullptr;
    const auto* S = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
    const auto* Clock = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    if (!S || !Clock) return false;
    const auto Timing = S->GetOwnerState();
    const double Now = Clock->GetServerWorldTimeSeconds();
    return Timing.bReady && FMath::IsFinite(Now) && FMath::IsFinite(Timing.DashEndServerSeconds)
        && FMath::IsFinite(Timing.CombatEndServerSeconds) && Timing.DashEndServerSeconds <= Now && Timing.CombatEndServerSeconds <= Now;
}

bool URPGRidingComponent::RequestDash()
{
    if (!CanRequestDash()) return false;
    UAbilitySystemComponent* ASC;
    const auto Handle = FindDashAbility(ASC);
    return Handle.IsValid() && ASC->TryActivateAbility(Handle);
}

ERPGRidingResultCode URPGRidingComponent::EvaluateDashAdmission(const FRPGRidingStateKey& Expected) const
{
    if (!IsInGameThread() || !IsBindingCurrent()) return ERPGRidingResultCode::AuthorityRevoked;
    if (RPGRiding::Compare(Expected, State.Key) != ERPGRidingStateOrder::Same) return ERPGRidingResultCode::StaleState;
    if (!RPGRiding::IsCanonical(State) || State.Mode != ERPGRidingMode::Mounted) return ERPGRidingResultCode::NotReady;
    if (State.Action != ERPGRidingAction::None && State.Action != ERPGRidingAction::Emote) return ERPGRidingResultCode::Busy;
    FRPGRidingEvaluationContext C; BuildGuards(C);
    if (!C.bReady) return ERPGRidingResultCode::NotReady;
    if (!C.bAlive || C.bActionBlocked || C.bSlowActive || Session->PendingDamageScopes.Num()) return ERPGRidingResultCode::ActionBlocked;
    if (!C.bOnGround) return ERPGRidingResultCode::WrongMovementMode;
    if (C.bInCombat) return ERPGRidingResultCode::InCombat;
    const auto* Definition = FindDefinition(State.MountId);
    if (!Definition || Definition->DefinitionVersion != State.DefinitionVersion) return ERPGRidingResultCode::DefinitionMismatch;
    if (!IsRegionAllowed(*Definition)) return ERPGRidingResultCode::RegionDenied;
    const auto Timing = Session->Runtime->GetTiming(FPlatformTime::Seconds());
    if (!Timing.bValid) return ERPGRidingResultCode::NotReady;
    if (Timing.DashRemaining > 0) return ERPGRidingResultCode::Busy;
    return ERPGRidingResultCode::Applied;
}

ERPGRidingResultCode URPGRidingComponent::TryBeginDash(FRPGRidingStateKey Expected, FVector Direction,
    TFunctionRef<bool(const FRPGRidingDashProfile&, const FVector&)> Preflight, FRPGRidingDashTicket& Out, FRPGRidingDashActivation Activation)
{
    Out = {};
    if (bPreparingDash || bApplying) return ERPGRidingResultCode::Busy;
    if (Direction.ContainsNaN() || Direction.Z != 0 || !Direction.IsNormalized()) return ERPGRidingResultCode::ActionBlocked;
    if (!Activation.IsEmpty() && !IsDashActivationCurrent(Activation)) return ERPGRidingResultCode::StaleState;
    auto Result = EvaluateDashAdmission(Expected);
    if (Result != ERPGRidingResultCode::Applied) return Result;
    if (State.Key.ActionSequence >= MAX_uint32 - 1) return ERPGRidingResultCode::NotReady;
    auto* S = Session.Get();
    FRPGRidingDashProfile Profile;
    if (!S->Runtime->CopyDashProfile(Profile)) return ERPGRidingResultCode::NotReady;
    TGuardValue<bool> Preparing(bPreparingDash, true);
    if (!Preflight(Profile, Direction)) return ERPGRidingResultCode::ActionBlocked;
    // The trusted executor may still invoke gameplay callbacks. Its old observation cannot approve a replacement.
    if (Session != S) return ERPGRidingResultCode::AuthorityRevoked;
    Result = EvaluateDashAdmission(Expected);
    if (Result != ERPGRidingResultCode::Applied) return Result;
    if (!Activation.IsEmpty() && !IsDashActivationCurrent(Activation)) return ERPGRidingResultCode::StaleState;
    const FGuid Execution = Activation.IsEmpty() ? FGuid::NewGuid() : Activation.ExecutionFor(Expected);
    if (!Execution.IsValid()) return ERPGRidingResultCode::StaleState;
    auto Approved = State;
    ++Approved.Key.ActionSequence;
    Approved.Action = ERPGRidingAction::Dash; Approved.ExecutionId = Execution;
    Approved.ActionDuration = Profile.Duration; Approved.ActionDirection = Direction;
    Approved.ActionStartServerWorldSeconds = GetWorld()->GetTimeSeconds();
    Approved.LastEndReason = ERPGRidingEndReason::None;
    const double Now = FPlatformTime::Seconds();
    if (!S->Runtime->TryReserveApprovedAction(Approved, Now)) return ERPGRidingResultCode::RateLimited;
    State = Approved;
    // Replace the action atomically: the old Emote's end callback cannot clear this Dash.
    ActiveEmote = {}; EmoteDeadline = 0;
    ActiveDash = {State.Key, State.ExecutionId, Profile}; DashDeadline = Now + Profile.Duration;
    Out = ActiveDash; // Remains an admitted ticket even if a publication callback immediately cancels it.
    S->PublishOwnerState();
    Publish();
    return ERPGRidingResultCode::Applied;
}

bool URPGRidingComponent::MatchesDash(const FRPGRidingDashTicket& Ticket) const
{
    return IsBindingCurrent() && State.Action == ERPGRidingAction::Dash && Ticket.ExecutionId.IsValid()
        && Ticket.ExecutionId == State.ExecutionId && Ticket.ExecutionId == ActiveDash.ExecutionId
        && RPGRiding::Compare(Ticket.Key, State.Key) == ERPGRidingStateOrder::Same;
}

bool URPGRidingComponent::IsDashCurrent(const FRPGRidingDashTicket& Ticket) const
{
    if (!IsInGameThread() || !MatchesDash(Ticket) || FPlatformTime::Seconds() >= DashDeadline) return false;
    FRPGRidingEvaluationContext C; BuildGuards(C);
    const auto* Definition = FindDefinition(State.MountId);
    return C.bReady && C.bAlive && !C.bActionBlocked && !C.bSlowActive && C.bOnGround && !C.bInCombat
        && !Session->PendingDamageScopes.Num() && Definition && Definition->DefinitionVersion == State.DefinitionVersion
        && IsRegionAllowed(*Definition);
}

bool URPGRidingComponent::FinishDash(const FRPGRidingDashTicket& Ticket, ERPGRidingEndReason Reason)
{
    if (!IsInGameThread() || !MatchesDash(Ticket)
        || (Reason != ERPGRidingEndReason::Completed && Reason != ERPGRidingEndReason::ExecutionRejected && Reason != ERPGRidingEndReason::Manual)) return false;
    DashRuntime->Stop(Reason, false);
    State.Action = ERPGRidingAction::None; State.ExecutionId.Invalidate();
    State.ActionDuration = 0; State.ActionStartServerWorldSeconds = 0; State.ActionDirection = FVector::ZeroVector;
    State.LastEndReason = Reason; ++State.Key.ActionSequence;
    ActiveDash = {}; DashDeadline = 0;
    Session->PublishOwnerState(); Publish();
    return true;
}

void URPGRidingComponent::ValidateServerMovementContext()
{
    if (!GetOwner() || !GetOwner()->HasAuthority()) return;
    RefreshBinding();
    if (!IsBindingCurrent() || State.Mode != ERPGRidingMode::Mounted) return;
    FRPGRidingEvaluationContext Context; BuildGuards(Context);
    if (!Context.bAlive) ForceDismount(ERPGRidingEndReason::Death);
    else if (!Context.bReady) ForceDismount(ERPGRidingEndReason::ContextLost);
    else if (Context.bActionBlocked || Context.bSlowActive) ForceDismount(ERPGRidingEndReason::Control);
    else if (!Context.bOnGround) ForceDismount(ERPGRidingEndReason::MovementMode);
    else if (const auto* Definition = FindDefinition(State.MountId); !Definition || !IsRegionAllowed(*Definition))
        ForceDismount(ERPGRidingEndReason::Region);
    UpdateEmote();
}

double URPGRidingComponent::ConstrainServerMovement(const FVector& Start, const FVector& End)
{
    const auto* Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Character->HasAuthority()) return 1;
    const bool WasMounted = State.Mode == ERPGRidingMode::Mounted;
    ValidateServerMovementContext();
    if (WasMounted && State.Mode != ERPGRidingMode::Mounted) return 0;
    const double DashFraction = DashRuntime->Constrain(Start, End);
    if (DashFraction <= 0 || State.Mode != ERPGRidingMode::Mounted) return DashFraction;
    if (Start.ContainsNaN() || End.ContainsNaN()
        || !Start.Equals(Character->GetCapsuleComponent()->GetComponentLocation(), 0.01))
    { ForceDismount(ERPGRidingEndReason::Region); return 0; }
    const auto* Definition = FindDefinition(State.MountId);
    const auto* Policy = GetWorld()->GetSubsystem<URPGRidingWorldPolicy>();
    const auto Result = Definition && Policy ? Policy->EvaluatePath(*Character, Start, Start + (End - Start) * DashFraction, Definition->RegionTags) : FRPGRidingPathPermission{};
    if (!Result.bAllowed)
    {
        const auto Epoch = State.Key.RiderEpoch;
        ForceDismount(ERPGRidingEndReason::Region);
        if (!IsBindingCurrent() || State.Key.RiderEpoch != Epoch
            || !Start.Equals(Character->GetCapsuleComponent()->GetComponentLocation(), 0.01)) return 0;
    }
    return DashFraction * (Result.bAllowed ? 1 : FMath::Clamp(Result.SafeFraction, 0., 1.));
}
void URPGRidingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding = true; ReleaseBinding();
    if (auto* Pawn = Cast<APawn>(GetOwner()))
    {
        Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
        Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &ThisClass::HandleRestarted);
    }
    Super::EndPlay(Reason);
}
void URPGRidingComponent::OnUnregister() { bEnding = true; ReleaseBinding(); Super::OnUnregister(); }
void URPGRidingComponent::OnRegister() { Super::OnRegister(); bEnding = false; }
