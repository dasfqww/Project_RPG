#include "Trading/RPGTradeTransport.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Trading/RPGTradeTargets.h"

FRPGHttpTradeTransport::FRPGHttpTradeTransport(FContext InContext, FValidate InValidate, float Timeout)
    : Context(MoveTemp(InContext)), Validate(MoveTemp(InValidate)),
      TimeoutSeconds(FMath::IsFinite(Timeout) ? FMath::Clamp(Timeout, 1.f, 30.f) : 10.f) {}
FRPGHttpTradeTransport::~FRPGHttpTradeTransport() { Cancel(); }
bool FRPGHttpTradeTransport::GetContext(FString& Endpoint, FString& Account) const
{
    FRPGTradeHttpContext Value;
    if (!Context || !Context(Value) || Value.Endpoint.IsEmpty() || Value.Account.IsEmpty()) return false;
    Endpoint = Value.Endpoint; Account = Value.Account;
    return true;
}
bool FRPGHttpTradeTransport::CanSend() const
{
    FRPGTradeHttpContext Value;
    return !Active && Context && Context(Value) && !Value.Authorization.IsEmpty();
}
bool FRPGHttpTradeTransport::Send(FGuid Character, FGuid Trade, const FString& Action, const FString& Body,
    TFunction<void(FRPGTradeHttpResult)> Completion)
{ return SendRequest(Character,Trade,Action,Body,MoveTemp(Completion),false); }
bool FRPGHttpTradeTransport::SendPreview(FGuid Character, FGuid Trade, TFunction<void(FRPGTradeHttpResult)> Completion)
{ return Trade.IsValid() && SendRequest(Character,Trade,TEXT(""),TEXT(""),MoveTemp(Completion),true); }
bool FRPGHttpTradeTransport::SendTargets(FGuid Character, const FString& Name, TFunction<void(FRPGTradeHttpResult)> Completion)
{
    FString Normalized;
    return RPGTradeTargets::NormalizeName(Name,Normalized) && SendRequest(Character,FGuid(),TEXT(""),TEXT(""),MoveTemp(Completion),false,Normalized);
}
bool FRPGHttpTradeTransport::SendRequest(FGuid Character, FGuid Trade, const FString& Action, const FString& Body,
    TFunction<void(FRPGTradeHttpResult)> Completion, bool bPreview, const FString& TargetName)
{
    FRPGTradeHttpContext Value;
    if (!IsInGameThread() || Active || !Character.IsValid() || !Completion || !Context || !Validate
        || !Context(Value) || Value.Endpoint.IsEmpty() || Value.Account.IsEmpty() || Value.Authorization.IsEmpty()) return false;
    const bool bWrite = !Action.IsEmpty();
    if (bWrite)
    {
        if (!Trade.IsValid() || Body.IsEmpty() || Body.Len() > 16384 || FTCHARToUTF8(*Body).Length() > 16384
            || !(Action == TEXT("invite") || Action == TEXT("accept") || Action == TEXT("offer") || Action == TEXT("ready")
                || Action == TEXT("withdraw") || Action == TEXT("confirm") || Action == TEXT("cancel"))) return false;
    }
    else if (!Body.IsEmpty()) return false;
    FString Path = TEXT("characters/") + Character.ToString(EGuidFormats::DigitsWithHyphensLower) + TEXT("/trades");
    if (Trade.IsValid()) Path += TEXT("/") + Trade.ToString(EGuidFormats::DigitsWithHyphensLower);
    if (bWrite) Path += TEXT("/") + Action;
    if (bPreview) Path += TEXT("/preview");
    if (!TargetName.IsEmpty()) Path += TEXT("/targets?name=")+FGenericPlatformHttp::UrlEncode(TargetName);
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetURL(Value.Endpoint / Path);
    Request->SetVerb(bWrite ? TEXT("POST") : TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
    Request->SetHeader(TEXT("Authorization"), Value.Authorization);
    Request->SetTimeout(TimeoutSeconds);
    if (bWrite) { Request->SetHeader(TEXT("Content-Type"), TEXT("application/json")); Request->SetContentAsString(Body); }
    const auto Expected = ++Generation;
    TWeakPtr<FRPGHttpTradeTransport> WeakThis = AsShared();
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, Expected, Value, bPreview, Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response, bool bSuccess) mutable
        {
            auto Self = WeakThis.Pin();
            if (!Self || Self->Generation != Expected) return;
            Self->Active.Reset();
            FRPGTradeHttpResult Result;
            Result.bTransportSuccessful = bSuccess && Response.IsValid();
            Result.StatusCode = Result.bTransportSuccessful ? Response->GetResponseCode() : 0;
            Result.bCurrentPlayerContext = Self->Validate(Value, Result.StatusCode);
            if (Result.bTransportSuccessful && Response->GetContent().Num() <= (bPreview ? 131072 : 32768)) Result.Body = Response->GetContentAsString();
            if (Result.StatusCode == 429 || Result.StatusCode == 503)
            {
                const FString Delay = Response->GetHeader(TEXT("Retry-After"));
                int32 Seconds = 0;
                bool bValid = !Delay.IsEmpty() && Delay.Len() <= 4;
                for (TCHAR C : Delay) { if (C < '0' || C > '9') { bValid = false; break; } Seconds = Seconds * 10 + C - '0'; }
                Result.RetryAfterSeconds = bValid ? FMath::Clamp(Seconds, 1, 3600) : 60;
            }
            Completion(MoveTemp(Result));
        });
    Active = Request;
    if (!Request->ProcessRequest() && Active == Request)
        Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    return true;
}
void FRPGHttpTradeTransport::Cancel()
{
    ++Generation;
    if (Active) { Active->OnProcessRequestComplete().Unbind(); Active->CancelRequest(); Active.Reset(); }
}
