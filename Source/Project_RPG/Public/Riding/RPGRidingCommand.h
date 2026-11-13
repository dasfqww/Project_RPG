#pragma once
#include "Riding/RPGRidingTypes.h"

enum class ERPGRidingPersistentOperation : uint8 { Register, Preferences };
struct PROJECT_RPG_API FRPGRidingIntentScope
{
    FString Endpoint, Account;
    FGuid Character;
    static bool Create(FString Endpoint, FString Account, FGuid Character, FRPGRidingIntentScope& Out);
    bool IsValid() const;
    bool operator==(const FRPGRidingIntentScope& Other) const
    { return Endpoint == Other.Endpoint && Account == Other.Account && Character == Other.Character; }
};
struct PROJECT_RPG_API FRPGRidingPreferencesCommand
{
    int64 ExpectedRevision = 0;
    FString SelectedMountId;
    TArray<FString> Favorites;
    FRPGRidingSelector Shortcut;
};
/** Original command only. Never contains authentication, inferred success or server authority. */
struct PROJECT_RPG_API FRPGRidingPendingIntent
{
    FRPGRidingIntentScope Scope;
    FGuid RequestId;
    ERPGRidingPersistentOperation Operation = ERPGRidingPersistentOperation::Register;
    FString Body;
    bool IsValid() const;
    FString Method() const;
    FString Path() const;
    bool operator==(const FRPGRidingPendingIntent& Other) const
    { return Scope == Other.Scope && RequestId == Other.RequestId && Operation == Other.Operation && Body == Other.Body; }
};
namespace RPGRidingCommand
{
    PROJECT_RPG_API bool Register(const FRPGRidingIntentScope& Scope, FGuid Request, FGuid Item, int64 ExpectedItemRevision, int32 CatalogVersion, FRPGRidingPendingIntent& Out);
    PROJECT_RPG_API bool Preferences(const FRPGRidingIntentScope& Scope, FGuid Request, const FRPGRidingPreferencesCommand& Command, FRPGRidingPendingIntent& Out);
}
