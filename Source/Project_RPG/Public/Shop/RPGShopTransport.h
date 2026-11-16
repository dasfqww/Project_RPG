#pragma once
#include "Shop/RPGShopTypes.h"
#include "HttpFwd.h"
struct PROJECT_RPG_API FRPGShopHttpContext { FString Endpoint, Account, Authorization; };
struct PROJECT_RPG_API FRPGShopHttpResult
{
    bool bCurrentPlayerContext=false, bTransportSuccessful=false;
    int32 StatusCode=0;
    FString Body;
    double RetryAfterSeconds=0;
};
class PROJECT_RPG_API IRPGShopTransport
{
public:
    using FCompletion=TFunction<void(FRPGShopHttpResult)>;
    virtual ~IRPGShopTransport()=default;
    virtual bool GetContext(FString& Endpoint,FString& Account) const=0;
    virtual bool CanSend() const=0;
    virtual bool ReadShop(FGuid Character,FCompletion Completion)=0;
    virtual bool SendIntent(const FRPGShopIntent& Intent,FCompletion Completion)=0;
    virtual bool ReadReceipt(const FRPGShopIntent& Intent,FCompletion Completion)=0;
    virtual bool ReadAssets(const FRPGShopIntent& Intent,FCompletion Completion)=0;
    virtual void Cancel()=0;
};
class PROJECT_RPG_API FRPGHttpShopTransport final : public IRPGShopTransport, public TSharedFromThis<FRPGHttpShopTransport>
{
public:
    using FContext=TFunction<bool(FRPGShopHttpContext&)>;
    using FValidate=TFunction<bool(const FRPGShopHttpContext&,int32)>;
    FRPGHttpShopTransport(FContext Context,FValidate Validate,float Timeout=10.f);
    virtual ~FRPGHttpShopTransport() override;
    virtual bool GetContext(FString&,FString&) const override;
    virtual bool CanSend() const override;
    virtual bool ReadShop(FGuid,FCompletion) override;
    virtual bool SendIntent(const FRPGShopIntent&,FCompletion) override;
    virtual bool ReadReceipt(const FRPGShopIntent&,FCompletion) override;
    virtual bool ReadAssets(const FRPGShopIntent&,FCompletion) override;
    virtual void Cancel() override;
private:
    bool Send(FGuid Character,const FString& Suffix,const FString& Body,int32 MaximumBytes,FCompletion Completion,const FRPGTradeIntentScope* Expected=nullptr);
    FContext Context;
    FValidate Validate;
    float TimeoutSeconds=10.f;
    FHttpRequestPtr Active;
    uint64 Generation=0;
};
