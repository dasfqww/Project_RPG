#pragma once
#include "Trading/RPGTradeClient.h"

struct PROJECT_RPG_API FRPGTradePreviewStat
{
    FString Tag;
    double Value = 0;
    bool operator==(const FRPGTradePreviewStat& O) const { return Tag == O.Tag && Value == O.Value; }
};
struct PROJECT_RPG_API FRPGTradePreviewGrowth
{
    FString Band;
    int32 RuleVersion = 0, Level = 0;
    bool operator==(const FRPGTradePreviewGrowth& O) const { return Band == O.Band && RuleVersion == O.RuleVersion && Level == O.Level; }
};
struct PROJECT_RPG_API FRPGTradeItemDetails
{
    FString DefinitionType, DefinitionName, BindState;
    int32 DefinitionVersion = 0, Durability = 0, MaximumDurability = 0;
    TArray<FString> Tags;
    TArray<FRPGTradePreviewStat> Stats;
    TOptional<FRPGTradePreviewGrowth> Growth;
    TOptional<FDateTime> ExpiresAt;
    bool operator==(const FRPGTradeItemDetails& O) const;
};
struct PROJECT_RPG_API FRPGTradePreviewItem
{
    FRPGTradeItem Offered;
    TOptional<FRPGTradeItemDetails> Details;
    bool operator==(const FRPGTradePreviewItem& O) const { return Offered == O.Offered && Details == O.Details; }
};
struct PROJECT_RPG_API FRPGTradePreview
{
    FRPGTradeSession Session;
    FDateTime ObservedAt;
    bool bOpen = false, bAllAvailable = false;
    TArray<FRPGTradePreviewItem> InitiatorItems, InviteeItems;
    // ObservedAt is freshness evidence, not a visible item/offer change.
    bool SameContent(const FRPGTradePreview& O) const;
    static bool Decode(const FString& Body, FGuid Character, const FRPGTradeSession& Expected, FRPGTradePreview& Out);
};
