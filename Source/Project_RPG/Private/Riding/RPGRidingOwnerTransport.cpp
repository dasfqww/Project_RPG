#include "Riding/RPGRidingOwnerTransport.h"
#include "Riding/RPGRidingCollection.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

namespace
{
    bool ValidContext(const FRPGRidingOwnerHttpContext& C, bool bCredential)
    {
        // ApiUrl is the existing API base, including /api. No arbitrary relative route is accepted.
        if (!(C.Endpoint.StartsWith(TEXT("http://")) || C.Endpoint.StartsWith(TEXT("https://")))
            || C.Endpoint.Contains(TEXT("?")) || C.Endpoint.Contains(TEXT("#")) || C.Endpoint.Contains(TEXT("@"))
            || C.Account.Len() != 17) return false;
        for (TCHAR Ch : C.Endpoint) if (FChar::IsWhitespace(Ch) || FChar::IsControl(Ch)) return false;
        for (TCHAR Ch : C.Account) if (Ch < '0' || Ch > '9') return false;
        return !bCredential || (C.Authorization.StartsWith(TEXT("Bearer ")) && C.Authorization.Len() > 7);
    }
}
FRPGHttpRidingOwnerTransport::FRPGHttpRidingOwnerTransport(FContext InContext, FValidate InValidate, float Timeout)
    : Context(MoveTemp(InContext)), Validate(MoveTemp(InValidate)), TimeoutSeconds(FMath::IsFinite(Timeout) ? FMath::Clamp(Timeout, 1.f, 30.f) : 10.f) {}
FRPGHttpRidingOwnerTransport::~FRPGHttpRidingOwnerTransport() { Cancel(); }
bool FRPGHttpRidingOwnerTransport::GetContext(FString& Endpoint, FString& Account) const
{
    Endpoint.Reset(); Account.Reset(); FRPGRidingOwnerHttpContext C;
    if (!IsInGameThread() || !Context || !Context(C) || !ValidContext(C, false)) return false;
    Endpoint = C.Endpoint; Account = C.Account; return true;
}
bool FRPGHttpRidingOwnerTransport::HasCredential() const
{
    FRPGRidingOwnerHttpContext C;
    return IsInGameThread() && Context && Validate && Context(C) && ValidContext(C, true);
}
bool FRPGHttpRidingOwnerTransport::CanSend() const { return !Active && HasCredential(); }
bool FRPGHttpRidingOwnerTransport::ReadCollection(FGuid Character, FCompletion Completion)
{ return Send(Character, TEXT("characters/") + Character.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT("/riding"), TEXT("GET"), FString(), MoveTemp(Completion)); }
bool FRPGHttpRidingOwnerTransport::ReadInventory(FGuid Character, FCompletion Completion)
{ return Send(Character, TEXT("items?ownerType=Character&ownerId=") + Character.ToString(EGuidFormats::DigitsWithHyphensLower)
    + TEXT("&includeTerminal=false&limit=500"), TEXT("GET"), FString(), MoveTemp(Completion), nullptr, 1024 * 1024); }
bool FRPGHttpRidingOwnerTransport::SendIntent(const FRPGRidingPendingIntent& I, FCompletion Completion)
{ return I.IsValid() && Send(I.Scope.Character, I.Path().Mid(5), I.Method(), I.Body, MoveTemp(Completion), &I.Scope); }
bool FRPGHttpRidingOwnerTransport::ReadReceipt(const FRPGRidingPendingIntent& I, FCompletion Completion)
{ return I.IsValid() && Send(I.Scope.Character, TEXT("characters/") + I.Scope.Character.ToString(EGuidFormats::DigitsWithHyphensLower)
    + TEXT("/riding/requests/") + I.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), TEXT("GET"), FString(), MoveTemp(Completion), &I.Scope); }
bool FRPGHttpRidingOwnerTransport::Send(FGuid Character, const FString& Path, const FString& Method, const FString& Body,
    FCompletion Completion, const FRPGRidingIntentScope* ExpectedScope, int32 MaximumBytes)
{
    FRPGRidingOwnerHttpContext C;
    if (!IsInGameThread() || Active || !Character.IsValid() || !Completion || !Context || !Validate || !Context(C) || !ValidContext(C, true)) return false;
    FRPGRidingIntentScope Scope;
    if (!FRPGRidingIntentScope::Create(C.Endpoint, C.Account, Character, Scope) || (ExpectedScope && !(Scope == *ExpectedScope))) return false;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetURL(Scope.Endpoint / Path);
    Request->SetVerb(Method); Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    if (!Body.IsEmpty()) { Request->SetHeader(TEXT("Content-Type"), TEXT("application/json")); Request->SetContentAsString(Body); }
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
    Request->SetHeader(TEXT("Authorization"), C.Authorization); Request->SetTimeout(TimeoutSeconds);
    const uint64 Expected = ++Generation; const TWeakPtr<FRPGHttpRidingOwnerTransport> Weak = AsShared();
    Request->OnProcessRequestComplete().BindLambda([Weak, Expected, C, MaximumBytes, Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess) mutable
    {
        auto Self = Weak.Pin(); if (!Self || Self->Generation != Expected) return;
        Self->Active.Reset(); FRPGRidingOwnerHttpResult Result;
        const bool bReceived = bSuccess && Response.IsValid(); const int32 Code = bReceived ? Response->GetResponseCode() : 0;
        Result.bCurrentPlayerContext = Self->Validate(C, Code);
        if (Result.bCurrentPlayerContext)
        {
            Result.bTransportSuccessful = bReceived; Result.StatusCode = Code;
            if (bReceived && Response->GetContent().Num() <= MaximumBytes) Result.Body = Response->GetContentAsString();
            if (bReceived && (Code == 429 || Code == 503))
            {
                const FString Text = Response->GetHeader(TEXT("Retry-After")); int32 Seconds = 0;
                bool bValid = !Text.IsEmpty() && Text.Len() <= 4;
                if (bValid) for (TCHAR Ch : Text) { if (Ch < '0' || Ch > '9') { bValid = false; break; } Seconds = Seconds * 10 + Ch - '0'; }
                Result.RetryAfterSeconds = bValid ? FMath::Clamp(Seconds, 1, 3600) : 60;
            }
        }
        Completion(MoveTemp(Result)); // Active is clear before any consumer can reenter.
    });
    Active = Request;
    if (!Request->ProcessRequest() && Active == Request) Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    return true;
}
void FRPGHttpRidingOwnerTransport::Cancel()
{
    ++Generation;
    if (Active) { Active->OnProcessRequestComplete().Unbind(); Active->CancelRequest(); Active.Reset(); }
}
