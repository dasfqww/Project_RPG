#pragma once
#include "Trading/RPGTradeClient.h"

enum class ERPGTradeJournalRead : uint8 { Missing, Ready, Busy, Invalid, Unavailable };

/** Windows local intent store, not authorization or an exchange receipt.
 * No credentials are written. Existing/invalid files are never overwritten.
 * Caller may clear only after a matching definitive result; loading never sends.
 */
class PROJECT_RPG_API FRPGTradeIntentJournal final
{
public:
    static ERPGTradeJournalRead Load(const FString& Root, const FRPGTradeIntentScope& Scope, FRPGTradePendingIntent& Out);
    static bool Create(const FString& Root, const FRPGTradePendingIntent& Intent);
    static bool Clear(const FString& Root, const FRPGTradePendingIntent& Expected);
    static bool ScopeKey(const FRPGTradeIntentScope& Scope, FString& OutKey);
};
