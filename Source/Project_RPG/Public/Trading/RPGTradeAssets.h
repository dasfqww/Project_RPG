#pragma once
#include "Trading/RPGTradeClient.h"
#include "Economy/Backend/RPGEconomyBackendTypes.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"

/** Current owned read projection; never a receipt delta or a client mutation. */
struct PROJECT_RPG_API FRPGTradeAssets
{
    FRPGEconomyWalletResult Wallet;
    TArray<FRPGInventoryProjectionEntry> Items, Mail;
    static bool Decode(const FString& Json, const FRPGTradeSession& Completed, FGuid Actor,
        const FString& Account, FRPGTradeAssets& Out);
};
