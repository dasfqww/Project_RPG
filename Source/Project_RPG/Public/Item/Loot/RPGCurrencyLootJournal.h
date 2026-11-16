#pragma once
#include "CoreMinimal.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
class IFileHandle;

/** Exclusive actor-lifetime lock and write-ahead original body. No credentials, no implicit replay. */
class PROJECT_RPG_API FRPGCurrencyLootJournal
{
public:
    FRPGCurrencyLootJournal();
    ~FRPGCurrencyLootJournal();
    bool Create(const FString& Root, const FRPGEconomyTransactionRequest& Request);
    bool Clear();
private:
    FString Path, Body;
    TUniquePtr<IFileHandle> Lock;
};
