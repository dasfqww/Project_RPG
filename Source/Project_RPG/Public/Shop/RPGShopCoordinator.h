#pragma once
#include "Shop/RPGShopTransport.h"
#include "Trading/RPGTradeAssets.h"

struct PROJECT_RPG_API FRPGShopContext
{
    FString Endpoint,Account;
    FGuid Character;
    bool bAuthenticated=false,bLocalLobby=false,bEligible=false;
};
struct PROJECT_RPG_API FRPGShopScreenView
{
    FRPGTradeIntentScope Context;
    FRPGShopSnapshot Snapshot;
    FGuid SnapshotToken,ReviewToken;
    FText Status,PendingText,ReviewText;
    bool bAvailable=false,bHasSnapshot=false,bBusy=false,bStorageBlocked=false,bHasPending=false,bHasReview=false;
    bool bCanTrade=false,bCanRefresh=false,bCanConfirm=false,bCanRecover=false,bCanRetry=false,bCanAssets=false,bCanStorage=false;
};
/** Single durable shop command owned independently of all windows. Ticking never dispatches. */
class PROJECT_RPG_API FRPGShopCoordinator final : public TSharedFromThis<FRPGShopCoordinator>
{
public:
    using FContext=TFunction<FRPGShopContext()>;
    FRPGShopCoordinator(TSharedRef<IRPGShopTransport>,FContext,FString JournalRoot,TFunction<double()> Clock={});
    ~FRPGShopCoordinator();
    void Tick();
    void Shutdown();
    bool BlocksTransitions();
    bool Refresh();
    bool Buy(FGuid SnapshotToken,const FString& OfferId,int32 Quantity);
    bool ReviewSale(FGuid SnapshotToken,FGuid Item,int32 Quantity);
    bool ConfirmSale(FGuid ReviewToken);
    void DismissSale();
    bool Recover();
    bool Retry();
    bool RefreshAssets();
    bool RecheckStorage();
    void SetAssetSink(TFunction<bool()> Begin,TFunction<bool(const FRPGTradeAssets&)> Apply,TFunction<bool()> Current);
    FRPGShopScreenView GetView();
    FSimpleMulticastDelegate OnViewChanged;
private:
    bool Sync();
    bool Load();
    bool Ready() const;
    bool NewCommandAllowed() const;
    bool Prepare(FRPGShopIntent Intent);
    bool Dispatch(bool bRecovery);
    bool ClearPending();
    void Notify();
    void Delay(const FRPGShopHttpResult&);
    TSharedRef<IRPGShopTransport> Transport;
    FContext ReadContext;
    FString JournalRoot;
    TFunction<double()> Clock;
    FRPGShopContext Current;
    FRPGTradeIntentScope Scope;
    TOptional<FRPGShopSnapshot> Snapshot;
    TOptional<FRPGShopIntent> Pending;
    TOptional<FRPGShopReceipt> Receipt;
    TOptional<FRPGShopIntent> SaleReview;
    FGuid SnapshotToken,ReviewToken;
    FText Status,PendingName;
    TFunction<bool()> AssetBegin,AssetCurrent;
    TFunction<bool(const FRPGTradeAssets&)> AssetApply;
    uint64 Generation=0;
    bool bBusy=false,bStorageBlocked=false,bUncertain=false,bEnding=false,bContextMismatch=false;
    double Now=0,NextAttempt=0,SnapshotAt=0;
};
