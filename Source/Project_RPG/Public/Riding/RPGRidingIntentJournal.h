#pragma once
#include "Riding/RPGRidingCommand.h"

enum class ERPGRidingJournalRead : uint8 { Missing, Ready, Busy, Invalid, Unavailable };
/** Windows write-ahead intent store. Loading never sends. Corrupt/existing records are never overwritten.
 * Clear requires the exact original intent and must be called only after a matching definitive result.
 * A timeout, missing receipt or local cancellation is not such a result. */
class PROJECT_RPG_API FRPGRidingIntentJournal final
{
public:
    static bool ScopeKey(const FRPGRidingIntentScope& Scope, FString& Out);
    static ERPGRidingJournalRead Load(const FString& Root, const FRPGRidingIntentScope& Scope, FRPGRidingPendingIntent& Out);
    static bool Create(const FString& Root, const FRPGRidingPendingIntent& Intent);
    static bool Clear(const FString& Root, const FRPGRidingPendingIntent& Expected);
};
