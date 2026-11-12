#include "Riding/RPGRidingLocalInteractionComponent.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingInteractionScope.h"
#include "Interface/InteractionInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"

namespace
{
    bool LivingAvatar(APawn* Pawn)
    {
        const auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
        if (!ASC || ASC->GetAvatarActor() != Pawn) return false;
        const auto Death = FGameplayTag::RequestGameplayTag(FName(TEXT("Shared.Status.Death")), false);
        if (Death.IsValid() && ASC->HasMatchingGameplayTag(Death)) return false;
        for (const auto* Set : ASC->GetSpawnedAttributes())
        {
            const FGameplayAttribute Health(Set ? FindFProperty<FProperty>(Set->GetClass(), TEXT("CurrentHealth")) : nullptr);
            if (Health.IsValid()) { const float Value = ASC->GetNumericAttribute(Health); return FMath::IsFinite(Value) && Value > 0; }
        }
        return false;
    }
}

URPGRidingLocalInteractionComponent::URPGRidingLocalInteractionComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickInterval = .05f;
}
bool URPGRidingLocalInteractionComponent::NeedsApproval(const APlayerController* Controller)
{
    if (!IsValid(Controller)) return false;
    const auto* Gate = Controller->FindComponentByClass<URPGRidingLocalInteractionComponent>();
    const APawn* Pawn = Controller->GetPawn(); const auto* R = Pawn ? Pawn->FindComponentByClass<URPGRidingComponent>() : nullptr;
    return (Gate && Gate->IsPending()) || (R && R->GetRidingState().Mode == ERPGRidingMode::Mounted);
}
bool URPGRidingLocalInteractionComponent::Prepare(AActor& InTarget, TFunction<void()> Continue)
{
    auto* PC = Cast<APlayerController>(GetOwner()); APawn* P = PC ? PC->GetPawn() : nullptr;
    auto* R = P ? P->FindComponentByClass<URPGRidingComponent>() : nullptr;
    auto* Interaction = IsValid(&InTarget) ? Cast<IInteractionInterface>(&InTarget) : nullptr;
    if (bEnding || bPreparing || IsPending() || !IsRegistered() || !IsValid(PC) || !PC->IsLocalController()
        || !IsValid(P) || !R || R->GetRidingState().Mode != ERPGRidingMode::Mounted || !Continue || !Interaction
        || LocalSequence == MAX_uint32 || !R->GetRidingState().Key.RiderEpoch.IsValid()
        || R->GetRidingState().Key.StateRevision >= MAX_uint32 - 1) return false;
    TGuardValue<bool> Preparing(bPreparing, true);
    Pawn = P; PlayerState = P->GetPlayerState(); Riding = R; Target = &InTarget;
    Expected = R->GetRidingState().Key; Identity = Interaction->GetRidingLocalInteractionIdentity();
    PendingSequence = ++LocalSequence; Continuation = MoveTemp(Continue); Deadline = FPlatformTime::Seconds() + 5;
    if (!Identity.IsValid() || !Matches()) { Cancel(); return false; }
    SetComponentTickEnabled(true); return true;
}
bool URPGRidingLocalInteractionComponent::Request(AActor& InTarget, TFunction<void()> Continue)
{
    if (IsPending()) return Target.Get() == &InTarget && Matches(); // Never replace a pending continuation.
    if (!Prepare(InTarget, MoveTemp(Continue))) return false;
    ServerPrepare(PendingSequence, Pawn.Get(), Target.Get(), Identity, Expected);
    return true;
}
bool URPGRidingLocalInteractionComponent::Matches() const
{
    const auto* PC = Cast<APlayerController>(GetOwner()); const auto* P = Pawn.Get(); const auto* T = Target.Get(); const auto* R = Riding.Get();
    const auto* Interaction = T ? Cast<IInteractionInterface>(T) : nullptr;
    if (bEnding || !IsRegistered() || !IsPending() || !IsValid(PC) || PC->IsActorBeingDestroyed() || !PC->IsLocalController()
        || !P || P->IsActorBeingDestroyed() || PC->GetPawn() != P || P->GetController() != PC
        || P->GetPlayerState() != PlayerState.Get() || !T || T->IsActorBeingDestroyed() || !R || !R->IsRegistered()
        || !GetWorld() || GetWorld()->bIsTearingDown || P->GetWorld() != GetWorld() || T->GetWorld() != GetWorld()
        || P->FindComponentByClass<URPGRidingComponent>() != R || !Interaction
        || Interaction->GetRidingLocalInteractionIdentity() != Identity || !Interaction->CanApproveRidingLocalInteraction(const_cast<APlayerController*>(PC))) return false;
    const auto State = R->GetRidingState();
    return State.Key.RiderEpoch == Expected.RiderEpoch && State.Key.StateRevision >= Expected.StateRevision
        && State.Key.StateRevision <= Expected.StateRevision + 1
        && (State.Mode != ERPGRidingMode::Mounted || State.Key.StateRevision == Expected.StateRevision);
}
void URPGRidingLocalInteractionComponent::ServerPrepare_Implementation(uint32 Sequence, APawn* ExpectedPawn,
    AActor* InTarget, FGuid InIdentity, FRPGRidingStateKey InExpected)
{
    auto Reject = [&] { ClientPrepared(Sequence, false, {}); };
    auto* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !PC->HasAuthority() || !Sequence || Sequence <= ServerSequence) return;
    ServerSequence = Sequence;
    const double Now = FPlatformTime::Seconds();
    if (bEnding || !IsRegistered() || Now < NextServerRequest) { Reject(); return; }
    NextServerRequest = Now + .2;
    auto* R = IsValid(ExpectedPawn) ? ExpectedPawn->FindComponentByClass<URPGRidingComponent>() : nullptr;
    auto* Interaction = IsValid(InTarget) ? Cast<IInteractionInterface>(InTarget) : nullptr;
    if (PC->GetPawn() != ExpectedPawn || !R || !Interaction || !InIdentity.IsValid()
        || R->GetRidingState().Mode != ERPGRidingMode::Mounted || !InExpected.RiderEpoch.IsValid()
        || R->GetRidingState().Key.RiderEpoch != InExpected.RiderEpoch
        || R->GetRidingState().Key.StateRevision != InExpected.StateRevision) { Reject(); return; }
    const auto Validate = [&]
    {
        return !bEnding && IsRegistered() && IsValid(PC) && PC->GetPawn() == ExpectedPawn
            && IsValid(InTarget) && InTarget->HasAuthority() && Cast<IInteractionInterface>(InTarget) == Interaction
            && Interaction->GetRidingLocalInteractionIdentity() == InIdentity
            && Interaction->CanApproveRidingLocalInteraction(PC) && LivingAvatar(ExpectedPawn);
    };
    if (!FRPGRidingInteractionScope::TryExecute(*ExpectedPawn, *InTarget, Validate, [&]
        { ClientPrepared(Sequence, true, R->GetRidingState().Key); return true; })) Reject();
}
void URPGRidingLocalInteractionComponent::ClientPrepared_Implementation(uint32 Sequence, bool bAccepted, FRPGRidingStateKey InApproved)
{
    if (!Sequence || Sequence != PendingSequence) return;
    if (!bAccepted || InApproved.RiderEpoch != Expected.RiderEpoch || InApproved.StateRevision != Expected.StateRevision + 1
        || InApproved.ActionSequence < Expected.ActionSequence) { Cancel(); return; }
    if (bApproved) return;
    bApproved = true; Approved = InApproved; Poll();
}
void URPGRidingLocalInteractionComponent::Poll()
{
    if (!IsPending()) return;
    if (FPlatformTime::Seconds() >= Deadline || !Matches()) { Cancel(); return; }
    if (!bApproved) return;
    const auto State = Riding->GetRidingState(); const auto Order = RPGRiding::Compare(State.Key, Approved);
    if (Order == ERPGRidingStateOrder::Older) return;
    if (Order != ERPGRidingStateOrder::Same || State.Mode != ERPGRidingMode::OnFoot) { Cancel(); return; }
    auto Continue = MoveTemp(Continuation); Cancel();
    if (Continue) Continue(); // Cleared before callbacks; duplicate/late results cannot invoke this again.
}
void URPGRidingLocalInteractionComponent::Cancel()
{
    PendingSequence = 0; Pawn.Reset(); PlayerState.Reset(); Target.Reset(); Riding.Reset();
    Identity.Invalidate(); Expected = {}; Approved = {}; bApproved = false; Deadline = 0; Continuation = nullptr;
    SetComponentTickEnabled(false);
}
void URPGRidingLocalInteractionComponent::TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function)
{ Super::TickComponent(Delta, Tick, Function); Poll(); }
void URPGRidingLocalInteractionComponent::OnUnregister() { Cancel(); Super::OnUnregister(); }
void URPGRidingLocalInteractionComponent::EndPlay(const EEndPlayReason::Type Reason)
{ bEnding = true; Cancel(); Super::EndPlay(Reason); }
