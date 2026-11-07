#pragma once

#include "CoreMinimal.h"

enum class ERPGTradeStatus : uint8 { Invited, Negotiating, Completed, Cancelled, Expired };
enum class ERPGTradeAction : uint8 { Invite, Accept, Offer, Ready, Withdraw, Confirm, Cancel };
enum class ERPGTradeDelivery : uint8 { Prepared, InFlight, Unknown };
enum class ERPGTradeResponse : uint8 { Ignored, Unknown, Applied, NoActiveTrade, Succeeded, Rejected };

struct PROJECT_RPG_API FRPGTradeItem
{
    FGuid ItemId;
    int64 ExpectedRevision = 0;
    int32 Quantity = 0;
    bool operator==(const FRPGTradeItem& Other) const;
};

struct PROJECT_RPG_API FRPGTradeOffer
{
    int64 CurrencyAmount = 0;
    TArray<FRPGTradeItem> Items;
    bool IsValid() const;
    bool IsEmpty() const { return CurrencyAmount == 0 && Items.IsEmpty(); }
    bool operator==(const FRPGTradeOffer& Other) const;
};

struct PROJECT_RPG_API FRPGTradeSession
{
    FGuid TradeId, InitiatorCharacterId, InviteeCharacterId;
    FString CurrencyCode;
    int64 Revision = 0, OfferRevision = 0;
    FDateTime ExpiresAt;
    ERPGTradeStatus Status = ERPGTradeStatus::Invited;
    FRPGTradeOffer InitiatorOffer, InviteeOffer;
    bool bInitiatorReady = false, bInviteeReady = false;
    bool bInitiatorConfirmed = false, bInviteeConfirmed = false;
    bool IsOpen() const { return Status == ERPGTradeStatus::Invited || Status == ERPGTradeStatus::Negotiating; }
    bool operator==(const FRPGTradeSession& Other) const;
};

// Capture with each callback. Account/character/endpoint changes invalidate ALL previous tickets.
struct PROJECT_RPG_API FRPGTradeTicket
{
    FGuid Generation;
    uint64 Sequence = 0;
    bool operator==(const FRPGTradeTicket& Other) const { return Generation == Other.Generation && Sequence == Other.Sequence; }
};

struct PROJECT_RPG_API FRPGTradeRequest
{
    FGuid RequestId, TradeId;
    FString RelativePath, Body;
    ERPGTradeDelivery Delivery = ERPGTradeDelivery::Prepared;
};

/** Game-thread state for the authenticated HTTP adapter. No UObject, credentials, timers or I/O.
 *  Pending requests are immutable until a matching receipt or a definitive FIRST-attempt refusal.
 *  Bind/Reset are identity boundaries, NOT a timeout/retry operation. In-memory lifetime only.
 */
class PROJECT_RPG_API FRPGTradeClient
{
public:
    bool Bind(FString Endpoint, const FString& AccountId, const FGuid& CharacterId);
    void Reset();
    const FRPGTradeSession* GetSession() const { return Session.IsSet() ? &Session.GetValue() : nullptr; }
    const FRPGTradeRequest* GetPending() const { return Pending.IsSet() ? &Pending.GetValue() : nullptr; }
    const FString& GetLastRejection() const { return LastRejection; }
    bool ClearClosedSession();

    bool PrepareInvite(const FGuid& InviteeCharacterId);
    bool Prepare(ERPGTradeAction Action, const FRPGTradeOffer* Offer = nullptr);
    // Copy path/body when dispatching. Retry after timeout/401/429/5xx uses the same request bytes.
    bool BeginAttempt(FRPGTradeTicket& OutTicket);
    ERPGTradeResponse CompleteAttempt(const FRPGTradeTicket& Ticket, int32 HttpStatus, const FString& Body);

    // Invalid TradeId selects GET active. A null active read NEVER resolves an uncertain write.
    FRPGTradeTicket BeginRead(const FGuid& TradeId = FGuid());
    ERPGTradeResponse CompleteRead(const FRPGTradeTicket& Ticket, int32 HttpStatus, const FString& Body);

private:
    bool MergeSession(const FRPGTradeSession& Incoming);
    bool StoreRequest(ERPGTradeAction Action, const FGuid& TradeId, const FGuid& Invitee, const FRPGTradeOffer* Offer);
    FGuid Generation = FGuid::NewGuid();
    uint64 Sequence = 0;
    FString BoundEndpoint, BoundAccount, LastRejection;
    FGuid BoundCharacter, PendingInvitee, ReadTradeId;
    FRPGTradeTicket AttemptTicket, ReadTicket;
    TOptional<FRPGTradeSession> Session;
    TOptional<FRPGTradeRequest> Pending;
    bool bHadUncertainAttempt = false;
};
