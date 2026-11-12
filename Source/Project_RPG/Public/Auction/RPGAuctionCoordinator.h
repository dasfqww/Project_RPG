#pragma once
#include "Auction/RPGAuctionTransport.h"
#include "Auction/RPGAuctionIntentJournal.h"

struct PROJECT_RPG_API FRPGAuctionLobbyContext
{
    FString Endpoint, Account;
    FGuid Character;
    bool bEnabled = false, bLocalLobby = false, bAuthenticated = false, bEligible = false;
};

/** Durable single-command owner. Loading/ticking never sends; recovery and retry are explicit.
 * Receipt assets are historical: a caller must refresh current assets before acknowledging them.
 */
class PROJECT_RPG_API FRPGAuctionCoordinator final : public TSharedFromThis<FRPGAuctionCoordinator>
{
public:
    using FContext = TFunction<FRPGAuctionLobbyContext()>;
    FRPGAuctionCoordinator(TSharedRef<IRPGAuctionTransport> Transport, FContext Context, FString JournalRoot, TFunction<double()> Clock = {});
    ~FRPGAuctionCoordinator();
    void Tick();
    void Shutdown();
    bool BlocksTransitions();
    bool Register(FGuid Listing, FGuid Item, int64 Revision, int32 Quantity, int64 Price, FDateTime Expires);
    bool Purchase(const FRPGAuctionListing& Listing);
    bool CancelListing(const FRPGAuctionListing& Listing);
    bool Claim(const FRPGAuctionPayable& Payable, int64 Amount);
    bool Retry();
    bool Recover();
    // Native projection owner only; do not call for receipt snapshots or a failed/stale read.
    bool AcknowledgeAssets(FGuid Request);
    const FRPGAuctionClient& GetClient() const { return Client; }
    bool IsStorageBlocked() const { return bStorageBlocked; }
    bool IsBusy() const { return bBusy; }
    double GetNextAttemptTime() const { return NextAttempt; }
    ERPGAuctionResponse GetLastResponse() const { return LastResponse; }
    TFunction<void()> OnChanged;
private:
    bool Sync();
    bool Reconcile();
    bool CanPrepare(bool bNewSale);
    bool SaveAndSend();
    bool Dispatch(bool bRecovery);
    void ResetMemory();
    void UpdateTime();
    void Notify();
    TSharedRef<IRPGAuctionTransport> Transport;
    FContext Context;
    FString JournalRoot;
    TFunction<double()> Clock;
    FRPGAuctionLobbyContext Current;
    FRPGTradeIntentScope Scope;
    FGuid BlockedSelection;
    FRPGAuctionClient Client;
    TOptional<FRPGAuctionIntent> ClearIntent;
    ERPGAuctionResponse LastResponse = ERPGAuctionResponse::Ignored;
    uint64 Generation = 0;
    bool bBusy = false, bStorageBlocked = false, bEnding = false;
    double Now = 0, NextAttempt = 0, NextStorage = 0;
};
