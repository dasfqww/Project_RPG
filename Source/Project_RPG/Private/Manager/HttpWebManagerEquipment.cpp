#include "Manager/HttpWebManager.h"

#include "Progression/RPGEquipmentMutationCodec.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "HAL/PlatformTime.h"

bool UHttpWebManager::GetPlayerEquipmentContext(FString& OutEndpoint, FString& OutSteamId) const
{
	OutEndpoint.Reset(); OutSteamId.Reset();
	if (!IsInGameThread() || IsRunningDedicatedServer() || IsConfiguredForDungeonServer()
		|| PlayerAuthentication->IsLoginActive() || PlayerAuthentication->IsLogoutActive()
		|| ApiUrl.IsEmpty() || !FRPGBackendPlayerAuthState::IsSteamId(GetAuthenticatedSteamId())) return false;
	OutEndpoint = ApiUrl; OutSteamId = GetAuthenticatedSteamId(); return true;
}

FHttpRequestRef UHttpWebManager::CreatePlayerEquipmentRequest(const FString& Endpoint, const FString& SteamId,
	const FString& Authorization, TFunction<void(FRPGEquipmentMutationHttpResult)> Completion)
{
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	// Never use ApplyCommonHeaders: only the authenticated PLAYER credential belongs here.
	Request->SetHeader(TEXT("Authorization"), Authorization);
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds) ? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis, Endpoint, SteamId, Authorization, Completion = MoveTemp(Completion)]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bTransport) mutable
	{
		FRPGEquipmentMutationHttpResult Result;
		auto* Manager = WeakThis.Get(); FString CurrentEndpoint, CurrentSteam;
		Result.bCurrentPlayerContext = Manager && Completed && Manager->GetPlayerEquipmentContext(CurrentEndpoint, CurrentSteam)
			&& CurrentEndpoint.Equals(Endpoint, ESearchCase::CaseSensitive) && CurrentSteam.Equals(SteamId, ESearchCase::CaseSensitive)
			&& Completed->GetHeader(TEXT("Authorization")).Equals(Authorization, ESearchCase::CaseSensitive)
			&& Manager->PlayerAuth.MatchesRequest(Authorization);
		Result.Response.bTransportSuccessful = bTransport && Response.IsValid();
		Result.Response.StatusCode = Response ? Response->GetResponseCode() : 0;
		if (Response && Response->GetContent().Num() <= 1024 * 1024) Result.Response.Body = Response->GetContentAsString();
		if (Result.bCurrentPlayerContext) Manager->PlayerAuth.InvalidateIfUnauthorized(Authorization, Result.Response.StatusCode);
		Completion(MoveTemp(Result));
	});
	return Request;
}

FHttpRequestPtr UHttpWebManager::CreatePlayerEquipmentMutationRequest(const FRPGEquipmentMutationCommand& Command,
	TFunction<void(FRPGEquipmentMutationHttpResult)> Completion)
{
	FString Endpoint, SteamId, Body, Fingerprint; FRPGEquipmentMutationScope Scope;
	if (!Completion || PlayerAuthentication->IsLoginActive() || !HasAccessToken()
		|| !GetPlayerEquipmentContext(Endpoint, SteamId) || !FRPGEquipmentMutationScope::Create(Endpoint, SteamId, Command.CharacterId, Scope)
		|| !FRPGEquipmentMutationCodec::Encode(Command, Body, Fingerprint)) return nullptr;
	const FString Authorization = PlayerAuth.Authorization(FPlatformTime::Seconds());
	if (Authorization.IsEmpty()) return nullptr;
	FHttpRequestRef Request = CreatePlayerEquipmentRequest(Endpoint, SteamId, Authorization, MoveTemp(Completion));
	Request->SetURL(Endpoint / FString::Printf(TEXT("characters/%s/equipment/%s"),
		*Command.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower), *FRPGEquipmentMutationCodec::Route(Command.Operation)));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);
	return Request;
}

FHttpRequestPtr UHttpWebManager::CreatePlayerEquipmentItemsRequest(FGuid CharacterId,
	TFunction<void(FRPGEquipmentMutationHttpResult)> Completion)
{
	FString Endpoint, SteamId; FRPGEquipmentMutationScope Scope;
	if (!Completion || !HasAccessToken() || !GetPlayerEquipmentContext(Endpoint, SteamId)
		|| !FRPGEquipmentMutationScope::Create(Endpoint, SteamId, CharacterId, Scope)) return nullptr;
	const FString Authorization = PlayerAuth.Authorization(FPlatformTime::Seconds());
	if (Authorization.IsEmpty()) return nullptr;
	FHttpRequestRef Request = CreatePlayerEquipmentRequest(Endpoint, SteamId, Authorization, MoveTemp(Completion));
	Request->SetURL(Endpoint / FString::Printf(TEXT("items?ownerType=Character&ownerId=%s&includeTerminal=false&limit=500"),
		*CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower)));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Cache-Control"), TEXT("no-cache, no-store"));
	return Request;
}
