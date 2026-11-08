#include "Riding/RPGRidingConsumeLease.h"
#include "Riding/RPGRidingComponent.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
    using FReservations = TMap<TWeakObjectPtr<APawn>, TSet<FGuid>>;
    FReservations& Reservations() { static FReservations Value; return Value; }
    void Prune()
    {
        for (auto It = Reservations().CreateIterator(); It; ++It)
            if (!It.Key().IsValid() || It.Value().IsEmpty()) It.RemoveCurrent();
    }
    void Release(TWeakObjectPtr<APawn> Pawn, const FGuid& Token)
    {
        if (auto* Tokens = Reservations().Find(Pawn))
        { Tokens->Remove(Token); if (Tokens->IsEmpty()) Reservations().Remove(Pawn); }
    }
}

FRPGRidingConsumeLease::FRPGRidingConsumeLease(APawn& InPawn, const FGuid& InToken) : Pawn(&InPawn), Token(InToken) {}

TSharedPtr<FRPGRidingConsumeLease> FRPGRidingConsumeLease::TryAcquire(APawn& Pawn)
{
    if (!IsInGameThread() || !IsValid(&Pawn) || Pawn.IsActorBeingDestroyed() || !Pawn.HasAuthority()
        || !Pawn.GetWorld() || Pawn.GetWorld()->bIsTearingDown || Pawn.GetWorld()->GetNetMode() == NM_Client) return {};
    const auto* Riding = Pawn.FindComponentByClass<URPGRidingComponent>();
    if (Riding && Riding->GetRidingState().Mode != ERPGRidingMode::OnFoot) return {};
    Prune();
    const TWeakObjectPtr<APawn> Key(&Pawn);
    const auto* Existing = Reservations().Find(Key);
    if ((Existing && Existing->Num() >= 64) || (!Existing && Reservations().Num() >= 1024)) return {};
    const FGuid Token = FGuid::NewGuid();
    Reservations().FindOrAdd(Key).Add(Token);
    return TSharedPtr<FRPGRidingConsumeLease>(new FRPGRidingConsumeLease(Pawn, Token));
}

bool FRPGRidingConsumeLease::HasPending(const APawn* Pawn)
{
    if (!IsInGameThread()) return true; // Gameplay must not query mutable server reservations from a worker.
    Prune();
    const auto* Tokens = Reservations().Find(TWeakObjectPtr<APawn>(const_cast<APawn*>(Pawn)));
    return Tokens && !Tokens->IsEmpty();
}

FRPGRidingConsumeLease::~FRPGRidingConsumeLease()
{
    if (IsInGameThread()) Release(Pawn, Token);
    else AsyncTask(ENamedThreads::GameThread, [Key = Pawn, Id = Token]() { Release(Key, Id); });
}
