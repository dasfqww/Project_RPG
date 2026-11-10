#pragma once
#include "Trading/RPGTradeClient.h"
#include "Trading/RPGTradeTransport.h"
#include "Trading/RPGTradePreview.h"
#include "Trading/RPGTradeTargets.h"

/** Local owner retains this across token refresh/reconnect. Call Pump on the game thread.
 * No autonomous polling or gameplay eligibility: the lobby/UI supplies selected character and reads.
 * Up to five automatic retries; after that explicit Retry resumes the SAME pending request.
 */
class PROJECT_RPG_API FRPGTradeCoordinator final : public TSharedFromThis<FRPGTradeCoordinator>
{
public:
    explicit FRPGTradeCoordinator(TSharedRef<IRPGTradeTransport> InTransport, TFunction<double()> InClock = {}, FString InJournalRoot = FString());
    ~FRPGTradeCoordinator();
    bool SelectCharacter(FGuid Character);
    void Reset();
    bool Refresh(FGuid Trade = FGuid());
    bool RefreshPreview();
    bool RefreshAssets(TFunction<void(FRPGTradeHttpResult)> Completion);
    const FRPGTradePreview* GetPreview() const;
    FGuid GetPreviewToken() const { return GetPreview() ? PreviewToken : FGuid(); }
    bool Invite(FGuid Invitee);
    bool SearchTargets(const FString& Name);
    void ClearTargets();
    bool CanSearchTargets() const;
    const TArray<FRPGTradeTarget>* GetTargets() const;
    FGuid GetTargetToken() const { return GetTargets() && !Targets.IsEmpty() ? TargetToken : FGuid(); }
    ERPGTradeTargetState GetTargetState() const { return TargetState; }
    bool Submit(ERPGTradeAction Action, const FRPGTradeOffer* Offer = nullptr);
    bool Retry();
    // Identity changes are still observed when lobby dispatch is paused.
    void Pump(bool bAllowDispatch = true);
    bool ClearClosedSession() { return !IsRecoveryBlocked() && Client.ClearClosedSession(); }
    bool HasPersistence() const { return !JournalRoot.IsEmpty(); }
    bool IsRecoveryBlocked() const { return HasPersistence() && Character.IsValid() && bPersistenceBlocked; }
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
    void InvalidatePreview();
    bool ReconcilePersistence();
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
    TOptional<FRPGTradePreview> Preview;
    FGuid PreviewToken;
    double PreviewDeadline = 0;
    TArray<FRPGTradeTarget> Targets;
    FGuid TargetToken;
    ERPGTradeTargetState TargetState = ERPGTradeTargetState::Idle;
    uint64 TargetGeneration = 0;
    double TargetDeadline = 0, NextSearch = 0;
    mutable double TargetNow = 0;
    FString JournalRoot;
    TOptional<FRPGTradePendingIntent> ClearIntent;
    bool bPersistenceBlocked = false;
    double NextPersistenceAttempt = 0;
};
