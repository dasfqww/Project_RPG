#include "Manager/HttpWebManager.h"
#include "Auction/RPGAuctionTransport.h"
#include "HAL/PlatformTime.h"

TSharedRef<IRPGAuctionTransport> UHttpWebManager::CreatePlayerAuctionTransport()
{
    const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
    return MakeShared<FRPGHttpAuctionTransport>(
        [WeakThis](FRPGAuctionHttpContext& Out)
        {
            auto* Web = WeakThis.Get();
            if (!IsInGameThread() || !Web || IsRunningDedicatedServer() || Web->IsConfiguredForDungeonServer()
                || !FRPGBackendPlayerAuthState::IsSteamId(Web->GetAuthenticatedSteamId()) || Web->ApiUrl.IsEmpty()) return false;
            Out.Endpoint = Web->ApiUrl; Out.Account = Web->GetAuthenticatedSteamId();
            FString Endpoint, Account;
            if (Web->GetPlayerEquipmentContext(Endpoint, Account)) Out.Authorization = Web->PlayerAuth.Authorization(FPlatformTime::Seconds());
            return true;
        },
        [WeakThis](const FRPGAuctionHttpContext& Captured, int32 Code)
        {
            auto* Web = WeakThis.Get();
            FString Endpoint, Account;
            const bool bCurrent = Web && Web->GetPlayerEquipmentContext(Endpoint, Account)
                && Endpoint == Captured.Endpoint && Account == Captured.Account
                && Web->PlayerAuth.MatchesRequest(Captured.Authorization);
            if (bCurrent) Web->PlayerAuth.InvalidateIfUnauthorized(Captured.Authorization, Code);
            return bCurrent;
        }, RequestTimeoutSeconds);
}
