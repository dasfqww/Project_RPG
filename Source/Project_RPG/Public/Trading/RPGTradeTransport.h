#pragma once
#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

struct PROJECT_RPG_API FRPGTradeHttpResult
{
    bool bCurrentPlayerContext = false;
    bool bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
    double RetryAfterSeconds = 0;
};

class PROJECT_RPG_API IRPGTradeTransport
{
public:
    virtual ~IRPGTradeTransport() = default;
    virtual bool GetContext(FString& Endpoint, FString& Account) const = 0;
    virtual bool CanSend() const = 0;
    virtual bool Send(FGuid Character, FGuid Trade, const FString& Action, const FString& Body,
        TFunction<void(FRPGTradeHttpResult)> Completion) = 0;
    virtual void Cancel() = 0;
    virtual bool SendPreview(FGuid Character, FGuid Trade, TFunction<void(FRPGTradeHttpResult)> Completion) { return false; }
};

// Transient credential copy for HTTP only. Never persist, log or expose this through UI state.
struct PROJECT_RPG_API FRPGTradeHttpContext
{
    FString Endpoint, Account, Authorization;
};

/** Game-thread HTTP adapter. Production callbacks come from UHttpWebManager's private player auth. */
class PROJECT_RPG_API FRPGHttpTradeTransport final : public IRPGTradeTransport, public TSharedFromThis<FRPGHttpTradeTransport>
{
public:
    using FContext = TFunction<bool(FRPGTradeHttpContext&)>;
    using FValidate = TFunction<bool(const FRPGTradeHttpContext&, int32)>;
    FRPGHttpTradeTransport(FContext InContext, FValidate InValidate, float Timeout = 10.f);
    virtual ~FRPGHttpTradeTransport() override;
    virtual bool GetContext(FString& Endpoint, FString& Account) const override;
    virtual bool CanSend() const override;
    virtual bool Send(FGuid Character, FGuid Trade, const FString& Action, const FString& Body,
        TFunction<void(FRPGTradeHttpResult)> Completion) override;
    virtual void Cancel() override;
    virtual bool SendPreview(FGuid Character, FGuid Trade, TFunction<void(FRPGTradeHttpResult)> Completion) override;
private:
    bool SendRequest(FGuid Character, FGuid Trade, const FString& Action, const FString& Body,
        TFunction<void(FRPGTradeHttpResult)> Completion, bool bPreview);
    FContext Context;
    FValidate Validate;
    float TimeoutSeconds;
    FHttpRequestPtr Active;
    uint64 Generation = 0;
};
