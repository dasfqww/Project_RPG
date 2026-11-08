#pragma once
#include "Trading/RPGTradeCoordinator.h"
#include "Trading/RPGTradeOfferDraft.h"

struct PROJECT_RPG_API FRPGTradeLobbyContext
{
    FString Endpoint, Account;
    FGuid Character;
    bool bEnabled = false, bLocalLobby = false, bAuthenticated = false, bEligible = false;
    bool operator==(const FRPGTradeLobbyContext& Other) const;
};
struct PROJECT_RPG_API FRPGTradeLobbyView
{
    FText Status, MyOffer, OtherOffer;
    FText MyDetails, OtherDetails;
    FGuid ReviewToken;
    bool bCanReview = false, bReady = false, bConfirm = false;
    bool bAccept = false, bCancel = false, bWithdraw = false, bRetry = false, bRefresh = false;
};
struct PROJECT_RPG_API FRPGTradeDraftView
{
    FGuid Token, BagToken;
    FString AmountText;
    FText Status;
    TArray<FRPGTradeBagItem> BagItems;
    TArray<FRPGTradeBagItem> SelectedItems;
    bool bCanReload = false, bCanBegin = false, bCanEdit = false, bCanSend = false;
};
struct PROJECT_RPG_API FRPGTradeTargetView
{
    FGuid InputToken, ResultToken;
    FString Query;
    FText Status;
    TArray<FRPGTradeTarget> Targets;
    bool bVisible=false, bCanEdit=false, bCanSearch=false, bCanInvite=false;
};
DECLARE_MULTICAST_DELEGATE(FRPGTradeLobbyChanged);

/** Shared by the local-player subsystem and short-lived views. Views hold weak references only. */
class PROJECT_RPG_API FRPGTradeLobbyOwner final : public TSharedFromThis<FRPGTradeLobbyOwner>
{
public:
    using FContext = TFunction<FRPGTradeLobbyContext()>;
    static TSharedRef<FRPGTradeLobbyOwner> Create(TSharedRef<IRPGTradeTransport> Transport, FContext Context, TFunction<double()> Clock = {});
    FRPGTradeLobbyOwner(TSharedRef<IRPGTradeTransport> Transport, FContext Context, TFunction<double()> Clock);
    ~FRPGTradeLobbyOwner();
    void Tick();
    void Shutdown();
    bool IsAvailable() const;
    bool BlocksTransitions() const;
    bool Refresh();
    bool Retry();
    bool Submit(ERPGTradeAction Action);
    bool SubmitConsent(ERPGTradeAction Action, FGuid ReviewedToken);
    using FDefinitionResolver = TFunction<bool(const FRPGTradeItemDetails&, FText&, FText&)>;
    void SetDefinitionResolver(FDefinitionResolver Resolver) { ResolveDefinition = MoveTemp(Resolver); }
    void SetBagSource(TFunction<FRPGTradeBagSnapshot()> Read, TFunction<bool()> Reload);
    void NotifyBagChanged() { Changed(); }
    FRPGTradeDraftView GetDraftView() const;
    bool ReloadBag();
    bool BeginDraft();
    void DiscardDraft();
    bool SetDraftAmount(FGuid Token, const FString& Amount);
    bool SetDraftItem(FGuid Token, FGuid Item, const FString& Quantity);
    bool RemoveDraftItem(FGuid Token, FGuid Item);
    bool SendDraft(FGuid Token);
    FRPGTradeTargetView GetTargetView() const;
    bool SetTargetName(FGuid InputToken, const FString& Name);
    bool SearchTargets(FGuid InputToken);
    bool InviteTarget(FGuid ResultToken, FGuid Character);
    void DismissClosed();
    FRPGTradeLobbyView GetView() const;
    const FRPGTradeClient& GetClient() const { return Coordinator->GetClient(); }
    FRPGTradeLobbyChanged OnChanged;
private:
    bool Update(bool bPoll);
    void Changed();
    bool HasLiveTrade() const;
    FContext Context;
    TFunction<double()> Clock;
    TSharedRef<FRPGTradeCoordinator> Coordinator;
    FRPGTradeLobbyContext Current;
    FString Account, Endpoint;
    FGuid BoundCharacter, BlockedSelection;
    double NextPoll = 0;
    bool bEnding = false;
    FDefinitionResolver ResolveDefinition;
    mutable FGuid LastPreviewToken, DisplayReviewToken;
    mutable FString LastReviewText;
    bool ReadBag(FRPGTradeBagSnapshot& Out) const;
    bool CanEditOffer() const;
    TFunction<FRPGTradeBagSnapshot()> BagReader;
    TFunction<bool()> BagReloader;
    FRPGTradeOfferDraft Draft;
    mutable double BagNow = 0;
    bool bSubmittingDraft = false;
    bool CanDiscover() const;
    FString TargetName;
    FGuid TargetInputToken=FGuid::NewGuid();
};
