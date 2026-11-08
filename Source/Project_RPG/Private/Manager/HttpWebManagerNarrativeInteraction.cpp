#include "Manager/HttpWebManager.h"
#include "Narrative/RPGNarrativeInteraction.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"

FHttpRequestPtr UHttpWebManager::CreateServerNarrativeInteractionRequest(const FRPGNarrativeInteractionCommand& Command,
    const FString& FrozenBody, TFunction<void(FRPGNarrativeInteractionHttpResult)> Completion)
{
    FGuid ConfiguredSession;
    if (!Completion || !Command.IsValid() || FrozenBody != Command.Encode() || !GetWorld() || GetWorld()->bIsTearingDown
        || GetWorld()->GetNetMode() == NM_Client || !IsConfiguredForDungeonServer()
        || !FGuid::Parse(ConfiguredDungeonSessionId, ConfiguredSession) || ConfiguredSession != Command.SessionId
        || BackendServiceToken.IsEmpty() || BackendServiceToken.Len() > 4096) return nullptr;
    for (const TCHAR C : BackendServiceToken) if (C <= ' ' || C >= 0x7f) return nullptr;
    const FString ExpectedServer = GameServerId, ExpectedSession = ConfiguredDungeonSessionId, ExpectedEndpoint = ApiUrl;
    const FString Authorization = TEXT("Bearer ") + BackendServiceToken;
    auto Request = FHttpModule::Get().CreateRequest();
    Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    Request->SetURL(ApiUrl / FString::Printf(TEXT("dungeon-sessions/%s/narrative/interactions/%s"),
        *Command.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Command.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower)));
    Request->SetVerb(TEXT("POST"));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
    Request->SetHeader(TEXT("Authorization"), Authorization);
    Request->SetContentAsString(FrozenBody);
    Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds) ? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
    const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
    Request->OnProcessRequestComplete().BindLambda(
        [WeakThis, ExpectedServer, ExpectedSession, ExpectedEndpoint, Authorization, Completion = MoveTemp(Completion)]
        (FHttpRequestPtr Completed, FHttpResponsePtr Response, const bool bTransport) mutable
        {
            FRPGNarrativeInteractionHttpResult Result;
            const auto* Manager = WeakThis.Get();
            Result.bCurrentServerContext = Manager && Completed && Manager->GetWorld() && !Manager->GetWorld()->bIsTearingDown
                && Manager->GetWorld()->GetNetMode() != NM_Client && Manager->IsConfiguredForDungeonServer()
                && Manager->GameServerId == ExpectedServer && Manager->ConfiguredDungeonSessionId == ExpectedSession
                && Manager->ApiUrl == ExpectedEndpoint && (TEXT("Bearer ") + Manager->BackendServiceToken) == Authorization;
            Result.StatusCode = Response ? Response->GetResponseCode() : 0;
            Result.bTransportSuccessful = bTransport && Response.IsValid();
            if (Response && Response->GetContent().Num() <= 256 * 1024) Result.Body = Response->GetContentAsString();
            Completion(MoveTemp(Result));
        });
    return Request;
}
