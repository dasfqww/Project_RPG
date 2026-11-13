#pragma once
#include "CoreMinimal.h"
#include "HttpFwd.h"

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
    virtual void Cancel() = 0;
};
/** One GET in flight, player credential only, explicit dispatch without automatic retry. */
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
    virtual void Cancel() override;
private:
    FContext Context;
    FValidate Validate;
    float TimeoutSeconds;
    FHttpRequestPtr Active;
    uint64 Generation = 0;
};
