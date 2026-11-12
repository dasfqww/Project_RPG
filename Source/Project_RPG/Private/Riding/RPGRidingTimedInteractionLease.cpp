#include "Riding/RPGRidingTimedInteractionLease.h"
#include "Riding/RPGRidingComponent.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
namespace
{
    TMap<TWeakObjectPtr<APawn>, FGuid>& Holds() { static TMap<TWeakObjectPtr<APawn>, FGuid> Value; return Value; }
    void PruneHolds() { for (auto It = Holds().CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent(); }
    void ReleaseHold(TWeakObjectPtr<APawn> Pawn, FGuid Token)
    { const auto* Current = Holds().Find(Pawn); if (Current && *Current == Token) Holds().Remove(Pawn); }
}
FRPGRidingTimedInteractionLease::FRPGRidingTimedInteractionLease(APawn& InPawn, FGuid InToken) : Pawn(&InPawn), Token(InToken) {}
bool FRPGRidingTimedInteractionLease::HasPending(const APawn* Pawn)
{
    if (!IsInGameThread()) return true;
    PruneHolds(); return Holds().Contains(TWeakObjectPtr<APawn>(const_cast<APawn*>(Pawn)));
}
TSharedPtr<FRPGRidingTimedInteractionLease> FRPGRidingTimedInteractionLease::TryAcquire(APawn& Pawn)
{
    if (!IsInGameThread() || !IsValid(&Pawn) || Pawn.IsActorBeingDestroyed() || !Pawn.HasAuthority()
        || !Pawn.GetWorld() || Pawn.GetWorld()->bIsTearingDown || Pawn.GetWorld()->GetNetMode() == NM_Client) return {};
    const auto* R = Pawn.FindComponentByClass<URPGRidingComponent>();
    if (!R || R->GetRidingState().Mode != ERPGRidingMode::OnFoot || HasPending(&Pawn) || Holds().Num() >= 1024) return {};
    const FGuid Token = FGuid::NewGuid(); Holds().Add(TWeakObjectPtr<APawn>(&Pawn), Token);
    return TSharedPtr<FRPGRidingTimedInteractionLease>(new FRPGRidingTimedInteractionLease(Pawn, Token));
}
FRPGRidingTimedInteractionLease::~FRPGRidingTimedInteractionLease()
{
    if (IsInGameThread()) ReleaseHold(Pawn, Token);
    else AsyncTask(ENamedThreads::GameThread, [Key = Pawn, Id = Token] { ReleaseHold(Key, Id); });
}
