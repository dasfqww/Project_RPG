#pragma once
#include "Riding/RPGRidingCommand.h"

/** Historical correlation evidence only; never apply receipt item records as current inventory. */
struct PROJECT_RPG_API FRPGRidingReceipt
{
    FGuid RequestId, Character, Roster;
    ERPGRidingPersistentOperation Operation = ERPGRidingPersistentOperation::Register;
    FString Outcome, MountId;
    int64 Revision = 0;
    FDateTime CommittedAt;
    bool bConsumedItem = false;
};
namespace RPGRidingReceipt
{
    PROJECT_RPG_API bool Decode(const FRPGRidingPendingIntent& Original, const FString& Body, bool bRecovery, FRPGRidingReceipt& Out);
    PROJECT_RPG_API bool DefinitiveFirstRejection(int32 Status, const FString& Body);
}
