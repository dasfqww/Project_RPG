#pragma once
#include "Trading/RPGTradePreview.h"

enum class ERPGAuctionStatus : uint8 { Active, Sold, Cancelled, Expired };
enum class ERPGAuctionAction : uint8 { Register, Purchase, Cancel, Claim };
enum class ERPGAuctionResponse : uint8 { Ignored, Unknown, Rejected, Succeeded };

struct PROJECT_RPG_API FRPGAuctionListing
{
    FGuid ListingId, SellerCharacter;
    int64 Revision = 0, TotalPrice = 0;
    FString CurrencyCode;
    int32 Quantity = 0;
    FDateTime ExpiresAt;
    ERPGAuctionStatus Status = ERPGAuctionStatus::Active;
    FRPGTradeItemDetails Item;
    TOptional<int64> SellerProceeds;
    bool bOwn = false;
};
struct PROJECT_RPG_API FRPGAuctionPage
{
    TArray<FRPGAuctionListing> Listings;
    FString NextCursor;
};
struct PROJECT_RPG_API FRPGAuctionPayable
{
    FGuid ListingId;
    FString CurrencyCode;
    int64 Revision = 0, Initial = 0, Remaining = 0;
};
struct PROJECT_RPG_API FRPGAuctionReceipt
{
    FGuid RequestId, ListingId;
    ERPGAuctionAction Action = ERPGAuctionAction::Register;
    TOptional<FRPGAuctionListing> Listing;
    int64 Revision = 0, Balance = 0, Paid = 0, Remaining = 0;
    FDateTime CommittedAt;
};
struct PROJECT_RPG_API FRPGAuctionRequest
{
    ERPGAuctionAction Action = ERPGAuctionAction::Register;
    FGuid RequestId, ListingId;
    FString RelativePath, Body;
    ERPGTradeDelivery Delivery = ERPGTradeDelivery::Prepared;
};
// Original bytes and identity scope only. Never contains bearer credentials or inferred outcomes.
struct PROJECT_RPG_API FRPGAuctionIntent
{
    FRPGTradeIntentScope Scope;
    FRPGAuctionRequest Request;
    bool IsValid() const;
};
namespace RPGAuction
{
    PROJECT_RPG_API bool DecodeListing(const FString& Body, bool bOwn, FRPGAuctionListing& Out);
    PROJECT_RPG_API bool DecodePage(const FString& Body, bool bOwn, FRPGAuctionPage& Out);
    PROJECT_RPG_API bool DecodePayable(const FString& Body, FRPGAuctionPayable& Out);
}

/** Game-thread model only. No UObject, HTTP, persistence, credentials or inventory delta application.
 *  Timeout/auth/throttle/5xx and any malformed success keep the original request for receipt recovery.
 */
class PROJECT_RPG_API FRPGAuctionClient
{
public:
    bool Bind(FString Endpoint, const FString& Account, FGuid Character);
    void Reset();
    bool PrepareRegister(FGuid Listing, FGuid Item, int64 ItemRevision, int32 Quantity, int64 Price, FDateTime Expires);
    bool PreparePurchase(const FRPGAuctionListing& Listing);
    bool PrepareCancel(const FRPGAuctionListing& Listing);
    bool PrepareClaim(const FRPGAuctionPayable& Payable, int64 RequestedAmount);
    const FRPGAuctionRequest* GetPending() const { return Pending.IsSet() ? &Pending.GetValue() : nullptr; }
    const FRPGAuctionReceipt* GetReceipt() const { return Receipt.IsSet() ? &Receipt.GetValue() : nullptr; }
    const FString& GetLastError() const { return LastError; }
    bool NeedsAssetRefresh() const { return bNeedsAssetRefresh; }
    bool AcknowledgeAssets(FGuid RequestId);
    bool ExportPending(FRPGAuctionIntent& Out) const;
    bool RestorePending(const FRPGAuctionIntent& Intent);
    bool BeginAttempt(FRPGTradeTicket& Out);
    ERPGAuctionResponse CompleteAttempt(const FRPGTradeTicket& Ticket, int32 Status, const FString& Body);
    bool BeginRecovery(FRPGTradeTicket& Out, FString& RelativePath);
    // A recovery 404 never proves that a pending transaction did not commit.
    ERPGAuctionResponse CompleteRecovery(const FRPGTradeTicket& Ticket, int32 Status, const FString& Body);
private:
    bool Prepare(ERPGAuctionAction Action, FGuid Listing, const TSharedPtr<class FJsonObject>& Obj);
    ERPGAuctionResponse Complete(int32 Status, const FString& Body, bool bRecovery);
    FRPGTradeIntentScope Scope;
    FGuid Generation = FGuid::NewGuid();
    uint64 Sequence = 0;
    FRPGTradeTicket Attempt, Recovery;
    TOptional<FRPGAuctionRequest> Pending;
    TOptional<FRPGAuctionReceipt> Receipt;
    FString LastError;
    bool bUncertain = false, bNeedsAssetRefresh = false;
};
