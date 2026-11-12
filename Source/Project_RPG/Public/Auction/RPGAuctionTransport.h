#pragma once
#include "Auction/RPGAuctionClient.h"
#include "Interfaces/IHttpRequest.h"

struct PROJECT_RPG_API FRPGAuctionSearch
{
    int32 Limit = 20;
    FString DefinitionName, CurrencyCode, Cursor;
    TOptional<int64> MinimumPrice, MaximumPrice;
};
struct PROJECT_RPG_API FRPGAuctionHttpResult
{
    bool bCurrentPlayerContext = false, bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
    double RetryAfterSeconds = 0;
};
// Transient, private-auth callback input only. Never journal or display this value.
struct PROJECT_RPG_API FRPGAuctionHttpContext { FString Endpoint, Account, Authorization; };

class PROJECT_RPG_API IRPGAuctionTransport
{
public:
    using FCompletion = TFunction<void(FRPGAuctionHttpResult)>;
    virtual ~IRPGAuctionTransport() = default;
    virtual bool GetContext(FString& Endpoint, FString& Account) const = 0;
    virtual bool CanSend() const = 0;
    virtual bool SendIntent(const FRPGAuctionIntent& Intent, FCompletion Completion) = 0;
    virtual bool SendPage(FGuid Character, const FRPGAuctionSearch& Search, bool bMine, FCompletion Completion) = 0;
    virtual bool SendListing(FGuid Character, FGuid Listing, FCompletion Completion) = 0;
    virtual bool SendPayable(FGuid Character, FGuid Listing, FCompletion Completion) = 0;
    virtual bool SendReceipt(FGuid Character, FGuid Request, FCompletion Completion) = 0;
    virtual bool SendAssets(FGuid Character, FGuid Request, FCompletion Completion) { return false; }
    virtual void Cancel() = 0;
};

/** Single-flight game-thread adapter. No retry, outcome inference, asset deltas or credential persistence. */
class PROJECT_RPG_API FRPGHttpAuctionTransport final : public IRPGAuctionTransport, public TSharedFromThis<FRPGHttpAuctionTransport>
{
public:
    using FContext = TFunction<bool(FRPGAuctionHttpContext&)>;
    using FValidate = TFunction<bool(const FRPGAuctionHttpContext&, int32)>;
    FRPGHttpAuctionTransport(FContext InContext, FValidate InValidate, float Timeout = 10.f);
    virtual ~FRPGHttpAuctionTransport() override;
    virtual bool GetContext(FString& Endpoint, FString& Account) const override;
    virtual bool CanSend() const override;
    virtual bool SendIntent(const FRPGAuctionIntent& Intent, FCompletion Completion) override;
    virtual bool SendPage(FGuid Character, const FRPGAuctionSearch& Search, bool bMine, FCompletion Completion) override;
    virtual bool SendListing(FGuid Character, FGuid Listing, FCompletion Completion) override;
    virtual bool SendPayable(FGuid Character, FGuid Listing, FCompletion Completion) override;
    virtual bool SendReceipt(FGuid Character, FGuid Request, FCompletion Completion) override;
    virtual bool SendAssets(FGuid Character, FGuid Request, FCompletion Completion) override;
    virtual void Cancel() override;
private:
    bool SendRequest(FGuid Character, const FString& Path, const FString& Body, int32 MaximumResponseBytes,
        FCompletion Completion, const FRPGTradeIntentScope* ExpectedScope = nullptr);
    FContext Context;
    FValidate Validate;
    float TimeoutSeconds;
    FHttpRequestPtr Active;
    uint64 Generation = 0;
};
