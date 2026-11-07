#include "Manager/HttpWebManager.h"
#include "Trading/RPGTradeTransport.h"
#include "HAL/PlatformTime.h"

TSharedRef<IRPGTradeTransport> UHttpWebManager::CreatePlayerTradeTransport()
{
    const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
    return MakeShared<FRPGHttpTradeTransport>(
        [WeakThis](FRPGTradeHttpContext& Out)
        {
            auto* Web = WeakThis.Get();
            if (!IsInGameThread() || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()
                || !FRPGBackendPlayerAuthState::IsSteamId(Web->GetAuthenticatedSteamId()) || Web->ApiUrl.IsEmpty()) return false;
            // Identity remains stable while a token is expired/being renewed or logout is in flight.
            Out.Endpoint = Web->ApiUrl; Out.Account = Web->GetAuthenticatedSteamId();
            FString Endpoint, Account;
            if (Web->GetPlayerEquipmentContext(Endpoint, Account)) Out.Authorization = Web->PlayerAuth.Authorization(FPlatformTime::Seconds());
            return true;
        },
        [WeakThis](const FRPGTradeHttpContext& Captured, int32 Code)
        {
            auto* Web = WeakThis.Get();
            const bool bCurrent = Web && Web->ApiUrl == Captured.Endpoint && Web->GetAuthenticatedSteamId() == Captured.Account
                && Web->PlayerAuth.MatchesRequest(Captured.Authorization);
            if (bCurrent) Web->PlayerAuth.InvalidateIfUnauthorized(Captured.Authorization, Code);
            return bCurrent;
        }, RequestTimeoutSeconds);
}
