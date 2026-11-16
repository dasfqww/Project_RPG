#pragma once
#include "Trading/RPGTradeClient.h"

struct PROJECT_RPG_API FRPGShopOffer
{
    FString OfferId, DefinitionType, DefinitionName;
    FText DisplayName;
    int32 DefinitionVersion=0, MaximumQuantity=0, MaximumStackSize=0, MaximumPurchasable=0;
    int64 UnitPrice=0;
};
struct PROJECT_RPG_API FRPGShopSaleItem
{
    FGuid ItemId;
    FString RuleId;
    FText DisplayName, Reason;
    int64 Revision=0, UnitPrice=0;
    int32 Quantity=0, MaximumQuantity=0;
    bool bCanSell=false;
};
struct PROJECT_RPG_API FRPGShopSnapshot
{
    FString ShopId, PolicyTag, CurrencyCode;
    FText DisplayName;
    int64 Balance=0, AvailableBalance=0;
    TArray<int32> InventoryFreeSlots;
    TArray<FRPGShopOffer> Offers;
    TArray<FRPGShopSaleItem> SaleItems;
};
/** Frozen consent and exact wire payload. No credentials or local asset mutations. */
struct PROJECT_RPG_API FRPGShopIntent
{
    FRPGTradeIntentScope Scope;
    FGuid RequestId, ItemId;
    FString ShopId, PolicyTag, Action, OfferId, CurrencyCode, Body;
    int32 Quantity=0;
    int64 UnitPrice=0, ItemRevision=0;
    bool IsValid() const;
    bool Same(const FRPGShopIntent& Other) const;
};
struct PROJECT_RPG_API FRPGShopReceipt
{
    FGuid RequestId;
    int64 BalanceBefore=0, BalanceAfter=0;
    FDateTime CommittedAt;
};
namespace RPGShopCodec
{
    PROJECT_RPG_API bool Snapshot(const FString& Json, FRPGShopSnapshot& Out);
    PROJECT_RPG_API bool Receipt(const FString& Json, const FRPGShopIntent& Expected, FRPGShopReceipt& Out);
    PROJECT_RPG_API bool Rejection(const FString& Json, const FRPGShopIntent& Expected, FString& ErrorCode);
    PROJECT_RPG_API bool Intent(const FRPGTradeIntentScope& Scope, const FString& Currency, const FString& Body, FRPGShopIntent& Out);
    PROJECT_RPG_API FString Body(const FRPGShopIntent& Intent);
    PROJECT_RPG_API FText Error(const FString& Code);
    PROJECT_RPG_API bool Definitive(const FString& Json, FString& Code);
}
