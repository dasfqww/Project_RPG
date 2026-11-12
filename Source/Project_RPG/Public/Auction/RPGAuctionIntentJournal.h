#pragma once
#include "Auction/RPGAuctionClient.h"

enum class ERPGAuctionJournalRead : uint8 { Missing, Ready, Busy, Invalid, Unavailable };

/** Windows local intent store, not authorization or an exchange receipt.
 * No credentials are written. Existing/invalid files are never overwritten.
 * Caller may clear only after a matching definitive result; loading never sends.
 */
class PROJECT_RPG_API FRPGAuctionIntentJournal final
{
public:
    static ERPGAuctionJournalRead Load(const FString& Root, const FRPGTradeIntentScope& Scope, FRPGAuctionIntent& Out);
    static bool Create(const FString& Root, const FRPGAuctionIntent& Intent);
    static bool Clear(const FString& Root, const FRPGAuctionIntent& Expected);
    static bool ScopeKey(const FRPGTradeIntentScope& Scope, FString& OutKey);
};
