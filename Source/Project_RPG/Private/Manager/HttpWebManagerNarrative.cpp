#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformTime.h"

FHttpRequestPtr UHttpWebManager::CreatePlayerNarrativeRequest(const FGuid CharacterId, const FString& ChoiceBody,
    TFunction<void(FRPGNarrativeHttpResult)> Completion)
{
    FString Endpoint, SteamId;
    if (!Completion || !CharacterId.IsValid() || ChoiceBody.Len() > 8192 || !HasAccessToken()
        || !GetPlayerEquipmentContext(Endpoint, SteamId)) return nullptr;
    const FString Authorization = PlayerAuth.Authorization(FPlatformTime::Seconds());
    if (Authorization.IsEmpty()) return nullptr;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    const bool bWrite = !ChoiceBody.IsEmpty();
    Request->SetURL(Endpoint / FString::Printf(TEXT("characters/%s/narrative%s"),
        *CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), bWrite ? TEXT("/choices") : TEXT("")));
    Request->SetVerb(bWrite ? TEXT("POST") : TEXT("GET"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
    Request->SetHeader(TEXT("Authorization"), Authorization);
    Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds) ? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
    if (bWrite) { Request->SetHeader(TEXT("Content-Type"), TEXT("application/json")); Request->SetContentAsString(ChoiceBody); }
    const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, Endpoint, SteamId, Authorization, Completion = MoveTemp(Completion)]
        (FHttpRequestPtr Completed, FHttpResponsePtr Response, const bool bTransport) mutable
        {
            FRPGNarrativeHttpResult Result;
            auto* Manager = WeakThis.Get(); FString CurrentEndpoint, CurrentSteam;
            Result.bCurrentPlayerContext = Manager && Completed
                && Manager->GetPlayerEquipmentContext(CurrentEndpoint, CurrentSteam)
                && CurrentEndpoint == Endpoint && CurrentSteam == SteamId && Manager->PlayerAuth.MatchesRequest(Authorization);
            Result.bTransportSuccessful = bTransport && Response.IsValid();
            Result.StatusCode = Response ? Response->GetResponseCode() : 0;
            if (Response && Response->GetContent().Num() <= 1024 * 1024) Result.Body = Response->GetContentAsString();
            if (Result.bCurrentPlayerContext) Manager->PlayerAuth.InvalidateIfUnauthorized(Authorization, Result.StatusCode);
            Completion(MoveTemp(Result));
        });
    return Request;
}
