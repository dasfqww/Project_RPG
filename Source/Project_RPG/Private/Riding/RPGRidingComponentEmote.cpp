#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "Riding/RPGRidingStatePolicy.h"
#include "Riding/RPGMountDefinition.h"
#include "Riding/RPGGameplayAbility_RidingEmote.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"

namespace
{
    bool IsMoving(const AActor* Actor)
    {
        const auto* Character = Cast<ACharacter>(Actor);
        const auto* Move = Character ? Character->GetCharacterMovement() : nullptr;
        return !Move || Move->Velocity.ContainsNaN() || Move->GetCurrentAcceleration().ContainsNaN()
            || Move->Velocity.SizeSquared2D() > 1. || Move->GetCurrentAcceleration().SizeSquared2D() > 1.;
    }
}

bool URPGRidingComponent::OwnsEmoteAbility(const UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle) const
{
    return ASC && IsBindingCurrent() && State.Mode == ERPGRidingMode::Mounted && ASC->GetAvatarActor() == GetOwner()
        && EmoteAbilityOwner.Get() == ASC && Handle.IsValid() && EmoteAbilityHandle == Handle
        && EmoteAbilityKey.RiderEpoch == State.Key.RiderEpoch && EmoteAbilityKey.StateRevision == State.Key.StateRevision;
}
void URPGRidingComponent::ReleaseEmoteAbility()
{
    auto* ASC = EmoteAbilityOwner.Get(); const auto Handle = EmoteAbilityHandle;
    EmoteAbilityOwner.Reset(); EmoteAbilityHandle = {}; EmoteAbilityKey = {};
    const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    if (Spec && Spec->SourceObject.Get() == this && Spec->Ability
        && Spec->Ability->GetClass() == URPGGameplayAbility_RidingEmote::StaticClass()) ASC->ClearAbility(Handle);
}
void URPGRidingComponent::SyncEmoteAbility()
{
    if (!GetOwner() || !GetOwner()->HasAuthority() || bSyncingEmoteAbility) return;
    TGuardValue<bool> Syncing(bSyncingEmoteAbility, true);
    const auto ReadyASC = [this]() -> UAbilitySystemComponent*
    {
        FRPGRidingEmoteProfile Profile;
        if (!IsRegistered() || !IsBindingCurrent() || State.Mode != ERPGRidingMode::Mounted
            || !Session->Runtime->CopyEmoteProfile(Profile)) return nullptr;
        auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), true);
        return IsValid(ASC) && ASC->GetAvatarActor() == GetOwner() && ASC->GetWorld() == GetWorld() ? ASC : nullptr;
    };
    auto* ASC = ReadyASC(); const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(EmoteAbilityHandle) : nullptr;
    if (OwnsEmoteAbility(ASC, EmoteAbilityHandle) && Spec && !Spec->PendingRemove && !Spec->RemoveAfterActivation
        && Spec->SourceObject.Get() == this && Spec->Ability && Spec->Ability->GetClass() == URPGGameplayAbility_RidingEmote::StaticClass()) return;
    ReleaseEmoteAbility(); ASC = ReadyASC();
    if (!ASC) return;
    FGameplayAbilitySpec NewSpec(URPGGameplayAbility_RidingEmote::StaticClass(), 1, INDEX_NONE, this);
    EmoteAbilityOwner = ASC; EmoteAbilityHandle = NewSpec.Handle; EmoteAbilityKey = State.Key;
    const auto Handle = NewSpec.Handle; ASC->GiveAbility(NewSpec);
    if (EmoteAbilityHandle == Handle && (ReadyASC() != ASC || !OwnsEmoteAbility(ASC, Handle))) ReleaseEmoteAbility();
}
FGameplayAbilitySpecHandle URPGRidingComponent::FindEmoteAbility(UAbilitySystemComponent*& OutASC) const
{
    OutASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), true);
    if (!IsValid(OutASC) || OutASC->GetAvatarActor() != GetOwner() || OutASC->GetWorld() != GetWorld()) return {};
    FGameplayAbilitySpecHandle Found;
    for (const auto& Spec : OutASC->GetActivatableAbilities())
    {
        if (Spec.SourceObject.Get() != this || !Spec.Ability || Spec.PendingRemove || Spec.RemoveAfterActivation
            || Spec.Ability->GetClass() != URPGGameplayAbility_RidingEmote::StaticClass()) continue;
        if (Found.IsValid()) return {};
        Found = Spec.Handle;
    }
    return Found;
}
bool URPGRidingComponent::CanRequestEmote() const
{
    const auto* Pawn = Cast<APawn>(GetOwner()); const auto* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
    if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(PC)
        || PC->GetPawn() != Pawn || PC->GetWorld() != GetWorld() || !RPGRiding::IsCanonical(State)
        || State.Mode != ERPGRidingMode::Mounted || State.Action != ERPGRidingAction::None
        || GetDashMovementState().Action == ERPGRidingAction::Dash || IsMoving(Pawn)) return false;
    UAbilitySystemComponent* ASC; const auto Handle = FindEmoteAbility(ASC);
    const auto* Spec = Handle.IsValid() ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
    if (!Spec || Spec->IsActive()) return false;
    if (Pawn->HasAuthority()) return OwnsEmoteAbility(ASC, Handle) && EvaluateEmoteAdmission() == ERPGRidingResultCode::Applied;
    if (Pawn->GetLocalRole() != ROLE_AutonomousProxy || !Pawn->IsLocallyControlled() || !PC->IsLocalController()) return false;
    const auto* PS = Pawn->GetPlayerState(); const auto* S = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
    const auto* Clock = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    if (!S || !Clock) return false;
    const auto Timing = S->GetOwnerState(); const double Now = Clock->GetServerWorldTimeSeconds();
    return Timing.bReady && FMath::IsFinite(Now) && FMath::IsFinite(Timing.EmoteEndServerSeconds)
        && FMath::IsFinite(Timing.CombatEndServerSeconds) && Timing.EmoteEndServerSeconds <= Now && Timing.CombatEndServerSeconds <= Now;
}
bool URPGRidingComponent::RequestEmote()
{
    if (!CanRequestEmote()) return false;
    UAbilitySystemComponent* ASC; const auto Handle = FindEmoteAbility(ASC);
    return Handle.IsValid() && ASC->TryActivateAbility(Handle);
}
ERPGRidingResultCode URPGRidingComponent::EvaluateEmoteAdmission() const
{
    if (!IsInGameThread() || !IsBindingCurrent()) return ERPGRidingResultCode::AuthorityRevoked;
    if (!RPGRiding::IsCanonical(State) || State.Mode != ERPGRidingMode::Mounted) return ERPGRidingResultCode::NotReady;
    if (State.Action != ERPGRidingAction::None || bApplying || bPreparingDash) return ERPGRidingResultCode::Busy;
    const auto* DashSpec = DashAbilityOwner.IsValid() ? DashAbilityOwner->FindAbilitySpecFromHandle(DashAbilityHandle) : nullptr;
    if (DashSpec && DashSpec->IsActive()) return ERPGRidingResultCode::Busy;
    FRPGRidingEvaluationContext Guards; BuildGuards(Guards);
    if (!Guards.bReady) return ERPGRidingResultCode::NotReady;
    if (!Guards.bAlive || Guards.bActionBlocked || Guards.bSlowActive || Session->PendingDamageScopes.Num() || IsMoving(GetOwner())) return ERPGRidingResultCode::ActionBlocked;
    if (!Guards.bOnGround) return ERPGRidingResultCode::WrongMovementMode;
    if (Guards.bInCombat) return ERPGRidingResultCode::InCombat;
    const auto* Definition = FindDefinition(State.MountId);
    if (!Definition || Definition->DefinitionVersion != State.DefinitionVersion) return ERPGRidingResultCode::DefinitionMismatch;
    if (!IsRegionAllowed(*Definition)) return ERPGRidingResultCode::RegionDenied;
    const auto Timing = Session->Runtime->GetTiming(FPlatformTime::Seconds());
    if (!Timing.bValid) return ERPGRidingResultCode::NotReady;
    return Timing.EmoteRemaining > 0 ? ERPGRidingResultCode::Busy : ERPGRidingResultCode::Applied;
}
bool URPGRidingComponent::BeginEmote(FRPGRidingEmoteTicket& Out)
{
    Out = {};
    if (EvaluateEmoteAdmission() != ERPGRidingResultCode::Applied || State.Key.ActionSequence >= MAX_uint32 - 1) return false;
    auto* ASC = EmoteAbilityOwner.Get(); const auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(EmoteAbilityHandle) : nullptr;
    FRPGRidingEmoteProfile Profile;
    if (!OwnsEmoteAbility(ASC, EmoteAbilityHandle) || !Spec || !Spec->IsActive() || !Session->Runtime->CopyEmoteProfile(Profile)) return false;
    auto Approved = State; ++Approved.Key.ActionSequence;
    Approved.Action = ERPGRidingAction::Emote; Approved.ExecutionId = FGuid::NewGuid();
    Approved.ActionDuration = Profile.Duration; Approved.ActionDirection = FVector::ZeroVector;
    Approved.ActionStartServerWorldSeconds = GetWorld()->GetTimeSeconds(); Approved.LastEndReason = ERPGRidingEndReason::None;
    const double Now = FPlatformTime::Seconds();
    if (!Session->Runtime->TryReserveApprovedAction(Approved, Now)) return false;
    State = Approved; ActiveEmote = {State.Key, State.ExecutionId}; EmoteDeadline = Now + Profile.Duration; Out = ActiveEmote;
    Session->PublishOwnerState(); Publish();
    return MatchesEmote(Out);
}
bool URPGRidingComponent::MatchesEmote(const FRPGRidingEmoteTicket& Ticket) const
{
    return IsBindingCurrent() && State.Action == ERPGRidingAction::Emote && Ticket.ExecutionId.IsValid()
        && State.ExecutionId == Ticket.ExecutionId && ActiveEmote.ExecutionId == Ticket.ExecutionId
        && RPGRiding::Compare(Ticket.Key, State.Key) == ERPGRidingStateOrder::Same;
}
bool URPGRidingComponent::FinishEmote(const FRPGRidingEmoteTicket& Ticket, ERPGRidingEndReason Reason)
{
    if (!IsInGameThread() || !MatchesEmote(Ticket)) return false;
    ActiveEmote = {}; EmoteDeadline = 0;
    State.Action = ERPGRidingAction::None; State.ExecutionId.Invalidate(); ++State.Key.ActionSequence;
    State.ActionDuration = 0; State.ActionStartServerWorldSeconds = 0; State.ActionDirection = FVector::ZeroVector;
    State.LastEndReason = Reason; Session->PublishOwnerState(); Publish(); return true;
}
void URPGRidingComponent::UpdateEmote()
{
    if (!MatchesEmote(ActiveEmote)) return;
    if (FPlatformTime::Seconds() >= EmoteDeadline) FinishEmote(ActiveEmote, ERPGRidingEndReason::Completed);
    else if (IsMoving(GetOwner())) FinishEmote(ActiveEmote, ERPGRidingEndReason::Manual);
}
