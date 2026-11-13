#include "Manager/HttpWebManager.h"
#include "Riding/RPGRidingOwnerTransport.h"
#include "HAL/PlatformTime.h"

TSharedRef<IRPGRidingOwnerTransport> UHttpWebManager::CreatePlayerRidingOwnerTransport()
{
    const TWeakObjectPtr<UHttpWebManager> Weak(this);
    return MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& Out)
    {
        auto* Web = Weak.Get();
        if (!Web || !Web->GetPlayerEquipmentContext(Out.Endpoint, Out.Account)) return false;
        Out.Authorization = Web->PlayerAuth.Authorization(FPlatformTime::Seconds()); return true;
    }, [Weak](const FRPGRidingOwnerHttpContext& Captured, int32 Code)
    {
        auto* Web = Weak.Get(); FString Endpoint, Account;
        const bool bCurrent = Web && Web->GetPlayerEquipmentContext(Endpoint, Account)
            && Endpoint == Captured.Endpoint && Account == Captured.Account && Web->PlayerAuth.MatchesRequest(Captured.Authorization);
        if (bCurrent) Web->PlayerAuth.InvalidateIfUnauthorized(Captured.Authorization, Code);
        return bCurrent;
    }, RequestTimeoutSeconds);
}
