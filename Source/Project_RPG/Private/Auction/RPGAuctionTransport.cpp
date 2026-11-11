#include "Auction/RPGAuctionTransport.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"

namespace
{
FString Root(FGuid Character)
{ return TEXT("characters/") + Character.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT("/auctions"); }
bool Query(const FRPGAuctionSearch& Q, FString& Out)
{
    if (Q.Limit < 1 || Q.Limit > 50 || Q.DefinitionName.Len() > 128 || Q.CurrencyCode.Len() > 64 || Q.Cursor.Len() > 512
        || (Q.MinimumPrice.IsSet() && Q.MinimumPrice.GetValue() <= 0) || (Q.MaximumPrice.IsSet() && Q.MaximumPrice.GetValue() <= 0)
        || (Q.MinimumPrice.IsSet() && Q.MaximumPrice.IsSet() && Q.MinimumPrice.GetValue() > Q.MaximumPrice.GetValue())) return false;
    if (!Q.DefinitionName.IsEmpty() && Q.DefinitionName.TrimStartAndEnd().IsEmpty()) return false;
    for (TCHAR C : Q.DefinitionName) if (FChar::IsControl(C)) return false;
    for (TCHAR C : Q.CurrencyCode) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '.' || C == '_' || C == '-')) return false;
    for (TCHAR C : Q.Cursor) if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_' || C == '-')) return false;
    Out = FString::Printf(TEXT("?limit=%d"), Q.Limit);
    auto Add = [&Out](const TCHAR* Name, const FString& Value) { if (!Value.IsEmpty()) Out += FString(TEXT("&")) + Name + TEXT("=") + FGenericPlatformHttp::UrlEncode(Value); };
    Add(TEXT("definitionName"), Q.DefinitionName); Add(TEXT("currencyCode"), Q.CurrencyCode); Add(TEXT("cursor"), Q.Cursor);
    if (Q.MinimumPrice.IsSet()) Add(TEXT("minimumPrice"), LexToString(Q.MinimumPrice.GetValue()));
    if (Q.MaximumPrice.IsSet()) Add(TEXT("maximumPrice"), LexToString(Q.MaximumPrice.GetValue()));
    return true;
}
}
FRPGHttpAuctionTransport::FRPGHttpAuctionTransport(FContext InContext, FValidate InValidate, float Timeout)
    : Context(MoveTemp(InContext)), Validate(MoveTemp(InValidate)), TimeoutSeconds(FMath::IsFinite(Timeout) ? FMath::Clamp(Timeout, 1.f, 30.f) : 10.f) {}
FRPGHttpAuctionTransport::~FRPGHttpAuctionTransport() { Cancel(); }
bool FRPGHttpAuctionTransport::GetContext(FString& Endpoint, FString& Account) const
{
    FRPGAuctionHttpContext Value;
    if (!IsInGameThread() || !Context || !Context(Value) || Value.Endpoint.IsEmpty() || Value.Account.IsEmpty()) return false;
    Endpoint = Value.Endpoint; Account = Value.Account; return true;
}
bool FRPGHttpAuctionTransport::CanSend() const
{
    FRPGAuctionHttpContext Value;
    return IsInGameThread() && !Active && Context && Validate && Context(Value) && !Value.Endpoint.IsEmpty() && !Value.Account.IsEmpty() && !Value.Authorization.IsEmpty();
}
bool FRPGHttpAuctionTransport::SendIntent(const FRPGAuctionIntent& Intent, FCompletion Completion)
{
    if (!Intent.IsValid()) return false;
    // ApiUrl already ends in /api. The validated intent's canonical path starts in /api/.
    return SendRequest(Intent.Scope.Character, Intent.Request.RelativePath.Mid(5), Intent.Request.Body, 4*1024*1024, MoveTemp(Completion), &Intent.Scope);
}
bool FRPGHttpAuctionTransport::SendPage(FGuid Character, const FRPGAuctionSearch& Search, bool bMine, FCompletion Completion)
{
    FString Suffix;
    return Query(Search, Suffix) && SendRequest(Character, Root(Character) + (bMine ? TEXT("/mine") : TEXT("")) + Suffix, TEXT(""), 2*1024*1024, MoveTemp(Completion));
}
bool FRPGHttpAuctionTransport::SendListing(FGuid Character, FGuid Listing, FCompletion Completion)
{ return Listing.IsValid() && SendRequest(Character, Root(Character) / Listing.ToString(EGuidFormats::DigitsWithHyphensLower), TEXT(""), 65536, MoveTemp(Completion)); }
bool FRPGHttpAuctionTransport::SendPayable(FGuid Character, FGuid Listing, FCompletion Completion)
{ return Listing.IsValid() && SendRequest(Character, Root(Character) / Listing.ToString(EGuidFormats::DigitsWithHyphensLower) / TEXT("payable"), TEXT(""), 65536, MoveTemp(Completion)); }
bool FRPGHttpAuctionTransport::SendReceipt(FGuid Character, FGuid Request, FCompletion Completion)
{ return Request.IsValid() && SendRequest(Character, Root(Character) / TEXT("requests") / Request.ToString(EGuidFormats::DigitsWithHyphensLower), TEXT(""), 4*1024*1024, MoveTemp(Completion)); }
bool FRPGHttpAuctionTransport::SendRequest(FGuid Character, const FString& Path, const FString& Body, int32 MaximumResponseBytes,
    FCompletion Completion, const FRPGTradeIntentScope* ExpectedScope)
{
    FRPGAuctionHttpContext Value;
    if (!IsInGameThread() || Active || !Character.IsValid() || !Completion || !Context || !Validate || !Context(Value)
        || Value.Endpoint.IsEmpty() || Value.Account.IsEmpty() || Value.Authorization.IsEmpty()) return false;
    FRPGTradeIntentScope Current;
    if (!FRPGTradeIntentScope::Create(Value.Endpoint, Value.Account, Character, Current) || (ExpectedScope && !(Current == *ExpectedScope))) return false;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetURL(Current.Endpoint / Path);
    Request->SetVerb(Body.IsEmpty() ? TEXT("GET") : TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
    Request->SetHeader(TEXT("Authorization"), Value.Authorization);
    Request->SetTimeout(TimeoutSeconds);
    if (!Body.IsEmpty()) { Request->SetHeader(TEXT("Content-Type"), TEXT("application/json")); Request->SetContentAsString(Body); }
    const uint64 Expected = ++Generation;
    TWeakPtr<FRPGHttpAuctionTransport> WeakThis = AsShared();
    Request->OnProcessRequestComplete().BindLambda([WeakThis, Expected, Value, MaximumResponseBytes, Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess) mutable
    {
        const auto Self = WeakThis.Pin();
        if (!Self || Self->Generation != Expected) return;
        Self->Active.Reset();
        FRPGAuctionHttpResult Result;
        const bool bReceived = bSuccess && Response.IsValid();
        const int32 Code = bReceived ? Response->GetResponseCode() : 0;
        Result.bCurrentPlayerContext = Self->Validate(Value, Code);
        if (Result.bCurrentPlayerContext)
        {
            Result.bTransportSuccessful = bReceived; Result.StatusCode = Code;
            if (bReceived && Response->GetContent().Num() <= MaximumResponseBytes) Result.Body = Response->GetContentAsString();
            if (bReceived && (Code == 429 || Code == 503))
            {
                const FString Delay = Response->GetHeader(TEXT("Retry-After"));
                int32 Seconds = 0;
                bool bValid = !Delay.IsEmpty() && Delay.Len() <= 4;
                if (bValid) for (TCHAR C : Delay) { if (C < '0' || C > '9') { bValid = false; break; } Seconds = Seconds * 10 + C - '0'; }
                Result.RetryAfterSeconds = bValid ? FMath::Clamp(Seconds, 1, 3600) : 60;
            }
        }
        Completion(MoveTemp(Result));
    });
    Active = Request;
    if (!Request->ProcessRequest() && Active == Request) Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    return true;
}
void FRPGHttpAuctionTransport::Cancel()
{
    ++Generation;
    if (Active) { Active->OnProcessRequestComplete().Unbind(); Active->CancelRequest(); Active.Reset(); }
}
