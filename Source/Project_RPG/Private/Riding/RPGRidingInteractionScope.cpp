#include "Riding/RPGRidingInteractionScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"

namespace
{
    TSet<TWeakObjectPtr<APawn>>& Pending() { static TSet<TWeakObjectPtr<APawn>> Value; return Value; }
    void Prune()
    {
        for (auto It = Pending().CreateIterator(); It; ++It)
            if (!It->IsValid()) It.RemoveCurrent();
    }
}

FRPGRidingInteractionScope::FRPGRidingInteractionScope(APawn& InPawn) : Pawn(&InPawn) { Pending().Add(Pawn); }
FRPGRidingInteractionScope::~FRPGRidingInteractionScope() { Pending().Remove(Pawn); }

bool FRPGRidingInteractionScope::HasPending(const APawn* Pawn)
{
    if (!IsInGameThread()) return true;
    Prune();
    return Pending().Contains(TWeakObjectPtr<APawn>(const_cast<APawn*>(Pawn)));
}

bool FRPGRidingInteractionScope::TryExecute(APawn& Pawn, AActor& Target,
    TFunctionRef<bool()> Validate, TFunctionRef<bool()> Execute)
{
    if (!IsInGameThread() || !IsValid(&Pawn) || Pawn.IsActorBeingDestroyed() || !Pawn.HasAuthority()
        || !Pawn.GetWorld() || Pawn.GetWorld()->bIsTearingDown || Pawn.GetWorld()->GetNetMode() == NM_Client
        || !IsValid(&Target) || Target.IsActorBeingDestroyed() || Target.GetWorld() != Pawn.GetWorld()
        || HasPending(&Pawn) || Pending().Num() >= 1024) return false;
    const TWeakObjectPtr<APawn> OriginalPawn(&Pawn);
    const TWeakObjectPtr<AActor> OriginalTarget(&Target);
    const TWeakObjectPtr<AController> Controller(Pawn.GetController());
    const TWeakObjectPtr<APlayerState> PlayerState(Pawn.GetPlayerState());
    const TWeakObjectPtr<URPGRidingComponent> Riding(Pawn.FindComponentByClass<URPGRidingComponent>());
    const bool bHadRiding = Riding.IsValid();
    auto Expected = bHadRiding ? Riding->State.Key : FRPGRidingStateKey{};
    const bool bWasMounted = bHadRiding && Riding->State.Mode == ERPGRidingMode::Mounted;
    const auto Matches = [&](const bool bOnFoot)
    {
        const auto* P = OriginalPawn.Get();
        const auto* C = Controller.Get();
        const auto* T = OriginalTarget.Get();
        const auto* R = Riding.Get();
        if (!P || P->IsActorBeingDestroyed() || !P->HasAuthority() || !C || C->IsActorBeingDestroyed()
            || !C->HasAuthority() || C->GetPawn() != P || P->GetController() != C || P->GetPlayerState() != PlayerState.Get()
            || !T || T->IsActorBeingDestroyed() || !P->GetWorld() || P->GetWorld()->bIsTearingDown
            || T->GetWorld() != P->GetWorld() || C->GetWorld() != P->GetWorld()
            || P->FindComponentByClass<URPGRidingComponent>() != R || bHadRiding != (R != nullptr)) return false;
        if (!R) return true;
        return R->IsRegistered() && !R->bEnding && !R->bApplying && !R->bPreparingDash
            && R->State.Key.RiderEpoch == Expected.RiderEpoch && R->State.Key.StateRevision == Expected.StateRevision
            && R->State.Key.ActionSequence == Expected.ActionSequence
            && (!Expected.RiderEpoch.IsValid() ? R->State.Mode == ERPGRidingMode::OnFoot : R->IsBindingCurrent())
            && (!bOnFoot || R->State.Mode == ERPGRidingMode::OnFoot);
    };
    // Reserve before calling validation: even validation/dismount callbacks cannot
    // recursively execute another interaction or overtake this operation with a mount.
    FRPGRidingInteractionScope Scope(Pawn);
    if (!Matches(false) || !Validate() || !Matches(false)) return false;
    if (bWasMounted)
    {
        if (Expected.StateRevision >= MAX_uint32 - 1) return false;
        ++Expected.StateRevision;
        Riding->ForceDismount(ERPGRidingEndReason::Manual);
    }
    if (!Matches(true) || !Validate() || !Matches(true)) return false;
    return Execute();
}
