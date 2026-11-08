#include "Riding/RPGRidingDamageScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Riding/RPGRidingSession.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "GenericTeamAgentInterface.h"
#include "HAL/PlatformTime.h"

FRPGRidingDamageScope::FRPGRidingDamageScope(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target)
{
    if (!IsInGameThread() || !IsValid(Source) || !IsValid(Target)) return;
    auto* SourcePawn = Cast<APawn>(Source->GetAvatarActor());
    auto* TargetPawn = Cast<APawn>(Target->GetAvatarActor());
    const auto Current = [](APawn* Pawn, UAbilitySystemComponent* ASC)
    {
        return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->HasAuthority()
            && Pawn->GetWorld() && !Pawn->GetWorld()->bIsTearingDown && Pawn->GetNetMode() != NM_Client
            && UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true) == ASC;
    };
    if (SourcePawn == TargetPawn || !Current(SourcePawn, Source) || !Current(TargetPawn, Target)
        || SourcePawn->GetWorld() != TargetPawn->GetWorld()) return;
    // Same controller-team-ID rule as RPGCombatFunctionLibrary::IsTargetPawnHostile.
    const auto* SourceTeam = Cast<IGenericTeamAgentInterface>(SourcePawn->GetController());
    const auto* TargetTeam = Cast<IGenericTeamAgentInterface>(TargetPawn->GetController());
    if (!SourceTeam || !TargetTeam || SourceTeam->GetGenericTeamId() == TargetTeam->GetGenericTeamId()) return;
    ControlSource = Source; ControlTarget = Target;
    ControlSourcePawn = SourcePawn; ControlTargetPawn = TargetPawn;
    for (auto* Pawn : {SourcePawn, TargetPawn})
    {
        auto* PS = Pawn->GetPlayerState();
        auto* Session = PS ? PS->FindComponentByClass<URPGRidingSessionComponent>() : nullptr;
        if (!Session || !Session->IsContextCurrent() || !Session->AuthorityContext.IsValid()
            || Session->BoundController.Get() != Pawn->GetController()
            || Pawn->GetController()->GetPawn() != Pawn) continue;
        auto& P = Participants.AddDefaulted_GetRef();
        P.Session = Session; P.Context = Session->AuthorityContext; P.Token = FGuid::NewGuid();
        P.Riding = Pawn->FindComponentByClass<URPGRidingComponent>();
        if (P.Riding.IsValid()) P.RiderEpoch = P.Riding->GetRidingState().Key.RiderEpoch;
        Session->PendingDamageScopes.Add(P.Token);
    }
}

void FRPGRidingDamageScope::Complete(float HealthLoss, float ShieldAbsorbed)
{
    check(IsInGameThread());
    if (bCompleted) return;
    bCompleted = true;
    if (!FMath::IsFinite(HealthLoss) || !FMath::IsFinite(ShieldAbsorbed)
        || HealthLoss < 0.f || ShieldAbsorbed < 0.f || (HealthLoss == 0.f && ShieldAbsorbed == 0.f)) return;
    CommitOutcome(ERPGRidingEndReason::Hit);
}

void FRPGRidingDamageScope::CompleteControl(FActiveGameplayEffectHandle Handle)
{
    check(IsInGameThread());
    if (bCompleted) return;
    bCompleted = true;
    auto* Source = ControlSource.Get(); auto* Target = ControlTarget.Get();
    auto* SourcePawn = ControlSourcePawn.Get(); auto* TargetPawn = ControlTargetPawn.Get();
    if (!Handle.IsValid() || !Source || !Target || !SourcePawn || !TargetPawn
        || Source->GetAvatarActor() != SourcePawn || Target->GetAvatarActor() != TargetPawn
        || SourcePawn->IsActorBeingDestroyed() || TargetPawn->IsActorBeingDestroyed()
        || !SourcePawn->HasAuthority() || !TargetPawn->HasAuthority()
        || !TargetPawn->GetWorld() || TargetPawn->GetWorld() != SourcePawn->GetWorld() || TargetPawn->GetWorld()->bIsTearingDown
        || UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SourcePawn, true) != Source
        || UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetPawn, true) != Target) return;
    const auto* Effect = Target->GetActiveGameplayEffect(Handle);
    if (!Effect || !Effect->Spec.Def || Effect->bIsInhibited || Effect->IsPendingRemove
        || Effect->Spec.GetContext().GetOriginalInstigatorAbilitySystemComponent() != Source) return;
    if (Effect->Spec.Def->DurationPolicy == EGameplayEffectDurationType::HasDuration
        && Effect->GetTimeRemaining(Target->GetWorld()->GetTimeSeconds()) <= 0.f) return;
    CommitOutcome(ERPGRidingEndReason::Control);
}

void FRPGRidingDamageScope::CommitOutcome(ERPGRidingEndReason Reason)
{
    const double Now = FPlatformTime::Seconds();
    // Reserve BOTH durable locks before publishing either dismount/callback.
    for (auto& P : Participants)
        if (auto* S = P.Session.Get(); S && S->AuthorityContext == P.Context
            && S->PendingDamageScopes.Contains(P.Token) && S->IsContextCurrent())
            P.bRecorded = S->Runtime->RecordApprovedHostileOutcome(Now);
    for (const auto& P : Participants)
    {
        auto* S = P.Session.Get();
        if (!P.bRecorded || !S || S->AuthorityContext != P.Context || !S->IsContextCurrent()) continue;
        S->PublishOwnerState();
        if (auto* Riding = P.Riding.Get(); Riding && P.RiderEpoch.IsValid()
            && Riding->GetRidingState().Key.RiderEpoch == P.RiderEpoch)
            Riding->ForceDismount(Reason);
    }
}

FRPGRidingDamageScope::~FRPGRidingDamageScope()
{
    check(IsInGameThread());
    for (const auto& P : Participants)
        if (auto* S = P.Session.Get()) S->PendingDamageScopes.Remove(P.Token);
}
