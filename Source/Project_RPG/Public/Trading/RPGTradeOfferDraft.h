#pragma once
#include "Trading/RPGTradeClient.h"

struct PROJECT_RPG_API FRPGTradeBagItem
{
    FGuid ItemId;
    int64 Revision = 0;
    int32 Quantity = 0;
    FText Label;
    bool bEligible = false;
};
struct PROJECT_RPG_API FRPGTradeBagSnapshot
{
    FString Endpoint, Account;
    FGuid Character, Token;
    double ValidUntil = 0;
    bool bReady = false, bLoading = false;
    TArray<FRPGTradeBagItem> Items;
    bool IsUsable(double Now) const;
    const FRPGTradeBagItem* Find(FGuid ItemId) const;
};

/** Editable expectations only. Binding changes never silently replace item revisions or rebase drafts. */
class PROJECT_RPG_API FRPGTradeOfferDraft
{
public:
    bool Begin(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag);
    void Reset();
    bool IsCurrent(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag) const;
    bool SetAmount(const FString& Text);
    bool SetItem(FGuid Item, int32 Quantity, const FRPGTradeBagSnapshot& Bag);
    bool RemoveItem(FGuid Item);
    bool Build(const FRPGTradeSession& Session, FGuid Actor, const FRPGTradeBagSnapshot& Bag, FRPGTradeOffer& Out) const;
    FGuid GetToken() const { return Token; }
    const FString& GetAmountText() const { return AmountText; }
    const TArray<FRPGTradeItem>& GetItems() const { return Offer.Items; }
    static bool ParseInteger(const FString& Text, int64& Out);
private:
    FRPGTradeSession Base;
    FGuid Character, BagToken, Token;
    FRPGTradeOffer Offer;
    FString AmountText;
    FString Endpoint, Account;
    bool bValidAmount = true;
};
