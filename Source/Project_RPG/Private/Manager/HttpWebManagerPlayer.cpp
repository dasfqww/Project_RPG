#include "Manager/HttpWebManager.h"

#include "HttpWebManagerRuntime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/PlatformTime.h"

namespace
{
	using RPGHttpWebManager::IsHttpBackendServerRuntime;
}

void UHttpWebManager::SetAccessToken(const FString& InAccessToken)
{
	if (IsPlayerLogoutInFlight()) return;
	PlayerAuth.SetOpaqueToken(InAccessToken);
}

void UHttpWebManager::ClearAccessToken()
{
	PlayerAuth.Invalidate();
}

bool UHttpWebManager::HasAccessToken() const { return PlayerAuthentication->HasAccessToken(); }
UObject& UHttpWebManager::GetAuthenticationOwner() const { return *const_cast<UHttpWebManager*>(this); }
FRPGBackendAuthenticationConfig UHttpWebManager::GetAuthenticationConfig() const
{
	return { ApiUrl, RequestTimeoutSeconds, IsHttpBackendServerRuntime(), IsPlayerProgressionRequestInFlight() };
}
void UHttpWebManager::ApplyAuthenticationHeaders(const FHttpRequestRef& Request) const { ApplyCommonHeaders(Request); }
void UHttpWebManager::ObserveAuthenticationResponse(const FHttpRequestPtr& Request, const FHttpResponsePtr& Response)
{ NotifyServiceDraining(Request, Response); }
void UHttpWebManager::BroadcastAuthenticationResult(bool bSuccess) { OnBackendAuthenticationCompleted.Broadcast(bSuccess); }
void UHttpWebManager::InvalidateAuthenticationDungeonRequests() { InvalidatePlayerDungeonRequests(); }

bool UHttpWebManager::RequestPlayerLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion)
{ return PlayerAuthentication->RequestLogout(MoveTemp(CanFinalize), MoveTemp(Completion)); }
void UHttpWebManager::OnPlayerLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{ PlayerAuthentication->HandleLogoutResponse(Request, Response, bWasSuccessful); }
void UHttpWebManager::AuthenticateWithSteamTicket(const FString& SteamTicketHex)
{ PlayerAuthentication->Authenticate(SteamTicketHex); }

void UHttpWebManager::LoadCharacters()
{
	if (!HasAccessToken())
	{
		const TArray<FRPGBackendCharacter> EmptyCharacters;
		OnCharactersLoaded.Broadcast(EmptyCharacters, false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnLoadCharactersResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("GET"));
	ApplyCommonHeaders(Request);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::CreateCharacter(const FString& CharacterName, const FString& ClassId)
{
	const FString NormalizedName = CharacterName.TrimStartAndEnd();
	if (!HasAccessToken() || NormalizedName.IsEmpty())
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("name"), NormalizedName);
	if (!ClassId.IsEmpty()) RootObject->SetStringField(TEXT("classId"), ClassId);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnCharacterCreated.Broadcast(FRPGBackendCharacter(), false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ThisClass::OnCreateCharacterResponseReceived);
	Request->SetURL(ApiUrl / TEXT("characters"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->ProcessRequest();
}

void UHttpWebManager::OnAuthenticateResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{ PlayerAuthentication->HandleLoginResponse(Request, Response, bWasSuccessful); }

bool UHttpWebManager::AcceptPlayerResponse(const FHttpRequestPtr& Request,
	const FHttpResponsePtr& Response, bool& bWasSuccessful)
{
	// Server callbacks share the dungeon transport, but use a distinct service credential.
	if (IsHttpBackendServerRuntime()) return true;
	if (!Request.IsValid()) return false;
	const FString Authorization = Request->GetHeader(TEXT("Authorization"));
	if (!PlayerAuth.MatchesRequest(Authorization))
	{
		// Expiry can fall between the operation guard and header construction. Complete that
		// unauthenticated request as failure, never applying its body or invalidating a new token.
		if (Authorization.IsEmpty() && !HasAccessToken())
		{
			bWasSuccessful = false;
			return true;
		}
		return false;
	}
	NotifyServiceDraining(Request, Response);
	PlayerAuth.InvalidateIfUnauthorized(Authorization,
		Response.IsValid() ? Response->GetResponseCode() : 0);
	// Concurrent requests must finish (releasing busy UI), but cannot apply data after expiry/401.
	if (!HasAccessToken()) bWasSuccessful = false;
	return true;
}

void UHttpWebManager::OnLoadCharactersResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	TArray<FRPGBackendCharacter> Characters;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TArray<TSharedPtr<FJsonValue>> CharacterValues;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, CharacterValues);
		if (bSuccess)
		{
			Characters.Reserve(CharacterValues.Num());
			for (const TSharedPtr<FJsonValue>& CharacterValue : CharacterValues)
			{
				FRPGBackendCharacter Character;
				if (!CharacterValue.IsValid()
					|| CharacterValue->Type != EJson::Object
					|| !FRPGBackendCharacter::Decode(
						CharacterValue->AsObject(),
						Character))
				{
					bSuccess = false;
					Characters.Reset();
					break;
				}
				Characters.Add(MoveTemp(Character));
			}
		}
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character list request failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharactersLoaded.Broadcast(Characters, bSuccess);
}

void UHttpWebManager::OnCreateCharacterResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!AcceptPlayerResponse(Request, Response, bWasSuccessful)) return;
	FRPGBackendCharacter Character;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& FRPGBackendCharacter::Decode(RootObject, Character);
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("Character creation failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	OnCharacterCreated.Broadcast(Character, bSuccess);
}
