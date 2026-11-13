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

bool UHttpWebManager::HasAccessToken() const
{
	return !IsPlayerLogoutInFlight() && PlayerAuth.HasAccessToken(FPlatformTime::Seconds());
}

bool UHttpWebManager::RequestPlayerLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion)
{
	if (!IsInGameThread() || IsHttpBackendServerRuntime() || IsPlayerLogoutInFlight() || ActiveAuthenticationRequest.IsValid()
		|| IsPlayerProgressionRequestInFlight() || !HasPlayerLogoutCredential() || !CanFinalize || !Completion) return false;
	const auto Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetURL(ApiUrl / TEXT("auth/logout"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), PlayerAuth.LogoutAuthorization());
	Request->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f));
	Request->OnProcessRequestComplete().BindUObject(this, &ThisClass::OnPlayerLogoutResponse);
	PlayerLogoutContextValidator = MoveTemp(CanFinalize);
	PlayerLogoutCompletion = MoveTemp(Completion);
	ActivePlayerLogoutRequest = Request;
	if (!Request->ProcessRequest()) OnPlayerLogoutResponse(Request, nullptr, false);
	return true;
}

void UHttpWebManager::OnPlayerLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	const bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActivePlayerLogoutRequest) return;
	const bool bSameCredential = PlayerAuth.MatchesRequest(Request->GetHeader(TEXT("Authorization")));
	const bool bConfirmed = bWasSuccessful && Response.IsValid()
		&& (Response->GetResponseCode() == 204 || Response->GetResponseCode() == 401);
	auto ValidateContext = MoveTemp(PlayerLogoutContextValidator);
	auto Completion = MoveTemp(PlayerLogoutCompletion);
	const bool bSuccess = bSameCredential && bConfirmed && ValidateContext && ValidateContext();
	if (bSuccess)
	{
		PlayerAuth.Reset();
		InvalidatePlayerDungeonRequests();
	}
	else if (bSameCredential && bConfirmed)
	{
		// The server may have revoked the token, but a changed world/selection
		// cannot authorize an account switch. Preserve the pinned identity.
		PlayerAuth.Invalidate();
	}
	ActivePlayerLogoutRequest.Reset();
	if (Completion) Completion(bSuccess);
}

void UHttpWebManager::AuthenticateWithSteamTicket(const FString& SteamTicketHex)
{
	// Duplicate calls must not complete/cancel the existing login.
	if (ActiveAuthenticationRequest.IsValid() || IsPlayerLogoutInFlight()) return;
	if (SteamTicketHex.IsEmpty())
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("ticket"), SteamTicketHex);

	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer))
	{
		OnBackendAuthenticationCompleted.Broadcast(false);
		return;
	}

	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->OnProcessRequestComplete().BindUObject(
		this, &ThisClass::OnAuthenticateResponseReceived);
	Request->SetURL(ApiUrl / TEXT("auth/steam-ticket"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	ApplyCommonHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(RequestTimeoutSeconds);
	ActiveAuthenticationRequest = Request;
	ActiveAuthenticationRequestStartedSeconds = FPlatformTime::Seconds();
	if (!Request->ProcessRequest() && ActiveAuthenticationRequest == Request)
	{
		ActiveAuthenticationRequest.Reset();
		ActiveAuthenticationRequestStartedSeconds = -1.0;
		OnBackendAuthenticationCompleted.Broadcast(false);
	}
}

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

void UHttpWebManager::OnAuthenticateResponseReceived(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActiveAuthenticationRequest) return;
	NotifyServiceDraining(Request, Response);
	const double RequestStartedSeconds = ActiveAuthenticationRequestStartedSeconds;
	ActiveAuthenticationRequest.Reset();
	ActiveAuthenticationRequestStartedSeconds = -1.0;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());

	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString NewAccessToken;
		FString SteamId;
		FString ExpiresAt;
		FString IssuedAt;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(TEXT("accessToken"), NewAccessToken)
			&& RootObject->TryGetStringField(TEXT("steamId"), SteamId)
			&& RootObject->TryGetStringField(TEXT("expiresAt"), ExpiresAt)
			&& RootObject->TryGetStringField(TEXT("issuedAt"), IssuedAt)
			&& PlayerAuth.AcceptLogin(NewAccessToken, SteamId, ExpiresAt, IssuedAt,
				RequestStartedSeconds, FPlatformTime::Seconds());
	}

	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Steam backend authentication failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}

	OnBackendAuthenticationCompleted.Broadcast(bSuccess);
}

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
