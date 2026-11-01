#include "Manager/HttpWebManager.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

void UHttpWebManager::LoadCharacterCreationOptions()
{
	const uint64 Generation = ++CharacterCreationOptionsGeneration;
	if (!HasAccessToken()) { OnCharacterCreationOptionsLoaded.Broadcast({}, false); return; }
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiUrl / TEXT("character-creation-options"));
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->OnProcessRequestComplete().BindUObject(this, &ThisClass::OnCharacterCreationOptionsResponse, Generation);
	if (!Request->ProcessRequest()) OnCharacterCreationOptionsLoaded.Broadcast({}, false);
}

void UHttpWebManager::OnCharacterCreationOptionsResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful, const uint64 Generation)
{
	if (Generation != CharacterCreationOptionsGeneration) return;
	// A newer credential can invalidate this latest request without replacing
	// it. Complete the failed read so its screen can offer a manual retry.
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful))
	{
		OnCharacterCreationOptionsLoaded.Broadcast({}, false);
		return;
	}
	TArray<FRPGCharacterCreationOption> Options;
	const bool bSuccess = bWasSuccessful && Response && Response->GetResponseCode() == 200
		&& FRPGCharacterCreationOption::DecodeList(Response->GetContentAsString(), Options);
	OnCharacterCreationOptionsLoaded.Broadcast(Options, bSuccess);
}
