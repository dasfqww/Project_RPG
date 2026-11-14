#pragma once
#include "CoreMinimal.h"
#include "HttpFwd.h"
#include "Riding/RPGRidingCommand.h"

/** Ephemeral private-auth input; never persist or expose in a collection/view. */
struct FRPGRidingOwnerHttpContext { FString Endpoint, Account, Authorization; };
struct FRPGRidingOwnerHttpResult
{
    bool bCurrentPlayerContext = false, bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
    double RetryAfterSeconds = 0;
};
class PROJECT_RPG_API IRPGRidingOwnerTransport
{
public:
    using FCompletion = TFunction<void(FRPGRidingOwnerHttpResult)>;
    virtual ~IRPGRidingOwnerTransport() = default;
    virtual bool GetContext(FString& Endpoint, FString& Account) const = 0;
    virtual bool HasCredential() const = 0;
    virtual bool CanSend() const = 0;
    virtual bool ReadCollection(FGuid Character, FCompletion Completion) = 0;
    virtual bool ReadInventory(FGuid Character, FCompletion Completion) { return false; }
    virtual bool ReadRegistrationOptions(FGuid Character, FCompletion Completion) { return false; }
    virtual bool SendIntent(const FRPGRidingPendingIntent&, FCompletion) { return false; }
    virtual bool ReadReceipt(const FRPGRidingPendingIntent&, FCompletion) { return false; }
    virtual void Cancel() = 0;
};
/** One request in flight, player credential only, explicit dispatch without automatic retry. */
class PROJECT_RPG_API FRPGHttpRidingOwnerTransport final : public IRPGRidingOwnerTransport, public TSharedFromThis<FRPGHttpRidingOwnerTransport>
{
public:
    using FContext = TFunction<bool(FRPGRidingOwnerHttpContext&)>;
    using FValidate = TFunction<bool(const FRPGRidingOwnerHttpContext&, int32)>;
    FRPGHttpRidingOwnerTransport(FContext InContext, FValidate InValidate, float Timeout = 10.f);
    virtual ~FRPGHttpRidingOwnerTransport() override;
    virtual bool GetContext(FString& Endpoint, FString& Account) const override;
    virtual bool HasCredential() const override;
    virtual bool CanSend() const override;
    virtual bool ReadCollection(FGuid Character, FCompletion Completion) override;
    virtual bool ReadInventory(FGuid Character, FCompletion Completion) override;
    virtual bool ReadRegistrationOptions(FGuid Character, FCompletion Completion) override;
    virtual bool SendIntent(const FRPGRidingPendingIntent& Intent, FCompletion Completion) override;
    virtual bool ReadReceipt(const FRPGRidingPendingIntent& Intent, FCompletion Completion) override;
    virtual void Cancel() override;
private:
    bool Send(FGuid Character, const FString& Path, const FString& Method, const FString& Body,
        FCompletion Completion, const FRPGRidingIntentScope* Expected = nullptr, int32 MaximumBytes = 256 * 1024);
    FContext Context;
    FValidate Validate;
    float TimeoutSeconds;
    FHttpRequestPtr Active;
    uint64 Generation = 0;
};
