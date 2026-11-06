#pragma once

#include "Riding/RPGRidingTypes.h"

enum class ERPGRidingRequestDisposition : uint8 { Reserved, Replayed, Pending, Rejected };

/** Historical receipt. Never install this snapshot as the latest replicated Pawn state. */
struct FRPGRidingReceipt
{
    ERPGRidingResultCode Result = ERPGRidingResultCode::StaleState;
    FRPGRidingRepState State;
};

struct FRPGRidingRequestToken
{
    FGuid Epoch;
    uint32 Sequence = 0;
    FGuid Reservation;
};

struct FRPGRidingRequestStart
{
    ERPGRidingRequestDisposition Disposition = ERPGRidingRequestDisposition::Rejected;
    FRPGRidingRequestToken Token;
    FRPGRidingReceipt Receipt;
};

/** Server-private, game-thread only. Validate/charge ingress before calling Begin. */
class FRPGRidingRequestLedger
{
public:
    static constexpr int32 Capacity = 32;
    void Reset(const FGuid& InEpoch);
    FRPGRidingRequestStart Begin(const FRPGRidingSetCommand& Command);
    bool Complete(const FRPGRidingRequestToken& Token, const FRPGRidingReceipt& Receipt);
    uint32 GetHighWater() const { return HighWater; }

private:
    struct FEntry
    {
        FRPGRidingSetCommand Command;
        FRPGRidingRequestToken Token;
        FRPGRidingReceipt Receipt;
        bool bPending = false;
    };
    FGuid Epoch;
    uint32 HighWater = 0;
    int32 NextSlot = 0;
    FEntry Entries[Capacity];
};
