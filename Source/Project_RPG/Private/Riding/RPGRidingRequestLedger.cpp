#include "Riding/RPGRidingRequestLedger.h"

namespace
{
    bool SameCommand(const FRPGRidingSetCommand& A, const FRPGRidingSetCommand& B)
    {
        return A.RiderEpoch == B.RiderEpoch && A.RequestSeq == B.RequestSeq
            && A.ExpectedStateRevision == B.ExpectedStateRevision && A.bDesiredMounted == B.bDesiredMounted
            && A.Selector.Kind == B.Selector.Kind && A.Selector.MountId == B.Selector.MountId;
    }
}

void FRPGRidingRequestLedger::Reset(const FGuid& InEpoch)
{
    Epoch = InEpoch;
    HighWater = 0;
    NextSlot = 0;
    for (FEntry& Entry : Entries) Entry = FEntry{};
}

FRPGRidingRequestStart FRPGRidingRequestLedger::Begin(const FRPGRidingSetCommand& Command)
{
    FRPGRidingRequestStart Start;
    if (!Epoch.IsValid() || Command.RiderEpoch != Epoch || !RPGRiding::IsValidCommand(Command)) return Start;
    if (Command.RequestSeq <= HighWater)
    {
        for (const FEntry& Entry : Entries)
        {
            if (Entry.Command.RequestSeq != Command.RequestSeq) continue;
            if (!SameCommand(Entry.Command, Command)) return Start;
            Start.Disposition = Entry.bPending ? ERPGRidingRequestDisposition::Pending : ERPGRidingRequestDisposition::Replayed;
            Start.Receipt = Entry.Receipt;
            // A duplicate cannot acquire the original execution's completion token.
            return Start;
        }
        return Start;
    }
    if (static_cast<uint64>(Command.RequestSeq) > static_cast<uint64>(HighWater) + 1 + 1024) return Start;
    for (int32 Offset = 0; Offset < Capacity; ++Offset)
    {
        const int32 Slot = (NextSlot + Offset) % Capacity;
        FEntry& Entry = Entries[Slot];
        if (Entry.bPending) continue; // Reentrant denials must not evict an active execution.
        Entry = FEntry{};
        Entry.Command = Command; // Bounded canonical fields only.
        Entry.Token = {Epoch, Command.RequestSeq, FGuid::NewGuid()};
        Entry.bPending = true;
        HighWater = Command.RequestSeq;
        NextSlot = (Slot + 1) % Capacity;
        Start.Disposition = ERPGRidingRequestDisposition::Reserved;
        Start.Token = Entry.Token;
        return Start;
    }
    Start.Receipt.Result = ERPGRidingResultCode::Busy;
    return Start;
}

bool FRPGRidingRequestLedger::Complete(const FRPGRidingRequestToken& Token, const FRPGRidingReceipt& Receipt)
{
    if (!Epoch.IsValid() || Token.Epoch != Epoch || !Token.Reservation.IsValid()
        || Receipt.State.Key.RiderEpoch != Epoch || !RPGRiding::IsCanonical(Receipt.State)
        || static_cast<uint8>(Receipt.Result) > static_cast<uint8>(ERPGRidingResultCode::AuthorityRevoked)) return false;
    for (FEntry& Entry : Entries)
    {
        if (!Entry.bPending || Entry.Token.Sequence != Token.Sequence || Entry.Token.Reservation != Token.Reservation) continue;
        Entry.Receipt = Receipt;
        Entry.bPending = false;
        return true;
    }
    return false;
}
