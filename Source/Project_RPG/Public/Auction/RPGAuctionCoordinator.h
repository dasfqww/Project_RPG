#pragma once
#include "Auction/RPGAuctionTransport.h"
#include "Auction/RPGAuctionIntentJournal.h"
#include "Trading/RPGTradeAssets.h"

enum class ERPGAuctionAssetsState : uint8 { Idle, Loading, Ready, Failed };
enum class ERPGAuctionReadState : uint8 { Idle, Loading, Ready, Failed };
struct PROJECT_RPG_API FRPGAuctionScreenRow { FGuid ListingId; FText Label; };
struct PROJECT_RPG_API FRPGAuctionScreenView
{
    // Native identity guard only; never rendered or persisted by the view.
    FRPGTradeIntentScope Context;
    FText Status, Detail, Assets;
    TArray<FRPGAuctionScreenRow> Rows;
    FGuid PageToken;
    bool bAvailable=false, bCanPublic=false, bCanMine=false, bCanNext=false, bCanDetail=false;
    bool bCanRecover=false, bCanRetry=false, bCanStorage=false, bCanAssets=false;
    bool bPublicVisible=false,bHasPending=false,bStorageBlocked=false,bHasReceipt=false;
};

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
    bool RecheckStorage();
    bool ReadPage(bool bMine);
    bool NextPage(FGuid DisplayedPage);
    bool ReadDetail(FGuid DisplayedPage,FGuid Listing);
    FRPGAuctionScreenView GetScreenView();
    void SetItemPresenter(TFunction<bool(const FRPGTradeItemDetails&,FText&,FText&)> Presenter) { ItemPresenter=MoveTemp(Presenter); }
    FSimpleMulticastDelegate OnViewChanged;
    void SetAssetSink(TFunction<bool()> Begin,TFunction<bool(const FRPGTradeAssets&)> Apply,TFunction<bool()> Current);
    bool ReloadAssets();
    const FRPGTradeAssets* GetAssets() const;
    ERPGAuctionAssetsState GetAssetsState() const { return AssetsState; }
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
    bool ReadAllowed(bool bMine);
    bool SearchPage(const FRPGAuctionSearch& Query,bool bMine);
    void ClearBrowse();
    void ReadDelay(const FRPGAuctionHttpResult& Result);
    FText ListingText(const FRPGAuctionListing& Listing,bool bDetail) const;
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
    TFunction<bool()> AssetBegin, AssetCurrent;
    TFunction<bool(const FRPGTradeAssets&)> AssetApply;
    TOptional<FRPGTradeAssets> Assets;
    ERPGAuctionAssetsState AssetsState = ERPGAuctionAssetsState::Idle;
    TOptional<FRPGAuctionPage> Page;
    TOptional<FRPGAuctionListing> Detail;
    FRPGAuctionSearch PageQuery;
    FGuid PageToken;
    bool bPageMine=false;
    ERPGAuctionReadState PageState=ERPGAuctionReadState::Idle, DetailState=ERPGAuctionReadState::Idle;
    TFunction<bool(const FRPGTradeItemDetails&,FText&,FText&)> ItemPresenter;
};
