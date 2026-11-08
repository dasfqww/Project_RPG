#pragma once
#include "Trading/RPGTradeClient.h"
#include "Trading/RPGTradeTransport.h"

/** Local owner retains this across token refresh/reconnect. Call Pump on the game thread.
 * No autonomous polling or gameplay eligibility: the lobby/UI supplies selected character and reads.
 * Up to five automatic retries; after that explicit Retry resumes the SAME pending request.
 */
class PROJECT_RPG_API FRPGTradeCoordinator final : public TSharedFromThis<FRPGTradeCoordinator>
{
public:
    explicit FRPGTradeCoordinator(TSharedRef<IRPGTradeTransport> InTransport, TFunction<double()> InClock = {});
    ~FRPGTradeCoordinator();
    bool SelectCharacter(FGuid Character);
    void Reset();
    bool Refresh(FGuid Trade = FGuid());
    bool Invite(FGuid Invitee);
    bool Submit(ERPGTradeAction Action, const FRPGTradeOffer* Offer = nullptr);
    bool Retry();
    // Identity changes are still observed when lobby dispatch is paused.
    void Pump(bool bAllowDispatch = true);
    bool ClearClosedSession() { return Client.ClearClosedSession(); }
    const FRPGTradeClient& GetClient() const { return Client; }
    bool IsBusy() const { return bBusy; }
    ERPGTradeResponse GetLastResponse() const { return LastResponse; }
    // Notifications run on the game thread; consumers may reenter or release their owner.
    TFunction<void()> OnChanged;
private:
    bool SyncContext();
    bool Dispatch();
    void Notify();
    void UpdateTime();
    TFunction<double()> Clock;
    TSharedRef<IRPGTradeTransport> Transport;
    FRPGTradeClient Client;
    FString Endpoint, Account, PendingAction;
    FGuid Character;
    uint64 Operation = 0;
    bool bBusy = false;
    double Now = 0, NextAttempt = 0;
    int32 Retries = 0;
    ERPGTradeResponse LastResponse = ERPGTradeResponse::Ignored;
};
