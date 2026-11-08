#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGRidingStatePolicy.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGRidingWorldPolicy.h"
#include "Riding/RPGRidingAbilityGate.h"
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
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

URPGRidingComponent::URPGRidingComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;
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
    const FGuid OldEpoch = State.Key.RiderEpoch;
    auto* Previous = Session.Get();
    Session.Reset(); Controller.Reset();
    if (Previous) Previous->Unbind(*this, OldEpoch);
    if (GetOwner() && GetOwner()->HasAuthority() && OldEpoch.IsValid())
    {
        State = {};
        Publish();
    }
}

void URPGRidingComponent::Publish()
{
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
    const auto Snapshot = State;
    OnChanged.Broadcast(Snapshot);
}

void URPGRidingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ThisClass, State);
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
    for (const auto& Spec : ASC->GetActivatableAbilities())
        if (Spec.IsActive() && FRPGRidingAbilityGate::BlocksMounting(Spec.Ability)) Out.bActionBlocked = true;
    const auto* Character = Cast<ACharacter>(Pawn);
    const auto* Move = Character ? Character->GetCharacterMovement() : nullptr;
    Out.bActionBlocked |= Character && Character->bIsCrouched;
    Out.bOnGround = Move && Move->MovementMode == MOVE_Walking;
    const auto Timing = Session->Runtime->GetTiming(FPlatformTime::Seconds());
    Out.bInCombat = !Timing.bValid || Timing.CombatRemaining > 0;
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
    auto Start = S->Runtime->BeginCommand(Command, State, FPlatformTime::Seconds());
    if (Start.Disposition == ERPGRidingRequestDisposition::Pending) return;
    if (Start.Disposition == ERPGRidingRequestDisposition::Reserved)
    {
        FRPGRidingEvaluationContext Context;
        BuildGuards(Context);
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
    const auto Key = State.Key;
    State = {};
    State.Key = Key;
    ++State.Key.StateRevision;
    State.LastEndReason = Reason;
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
}

double URPGRidingComponent::ConstrainServerMovement(const FVector& Start, const FVector& End)
{
    const auto* Character = Cast<ACharacter>(GetOwner());
    if (!Character || !Character->HasAuthority() || State.Mode != ERPGRidingMode::Mounted) return 1;
    ValidateServerMovementContext();
    if (State.Mode != ERPGRidingMode::Mounted) return 0;
    if (Start.ContainsNaN() || End.ContainsNaN()
        || !Start.Equals(Character->GetCapsuleComponent()->GetComponentLocation(), 0.01))
    { ForceDismount(ERPGRidingEndReason::Region); return 0; }
    const auto* Definition = FindDefinition(State.MountId);
    const auto* Policy = GetWorld()->GetSubsystem<URPGRidingWorldPolicy>();
    const auto Result = Definition && Policy ? Policy->EvaluatePath(*Character, Start, End, Definition->RegionTags) : FRPGRidingPathPermission{};
    if (!Result.bAllowed)
    {
        const auto Epoch = State.Key.RiderEpoch;
        ForceDismount(ERPGRidingEndReason::Region);
        if (!IsBindingCurrent() || State.Key.RiderEpoch != Epoch
            || !Start.Equals(Character->GetCapsuleComponent()->GetComponentLocation(), 0.01)) return 0;
    }
    return Result.bAllowed ? 1 : FMath::Clamp(Result.SafeFraction, 0., 1.);
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
