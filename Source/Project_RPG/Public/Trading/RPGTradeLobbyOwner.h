#pragma once
#include "Trading/RPGTradeCoordinator.h"

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
    bool bAccept = false, bCancel = false, bWithdraw = false, bRetry = false, bRefresh = false;
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
};
