#include "Online/RPGBackendAuthentication.h"

#include "Online/RPGBackendPlayerAuthState.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

FRPGBackendAuthentication::~FRPGBackendAuthentication()
{
	CancelLogout();
	CancelLogin();
}

bool FRPGBackendAuthentication::HasAccessToken() const
{
	return !IsLogoutActive() && State.HasAccessToken(FPlatformTime::Seconds());
}

void FRPGBackendAuthentication::CancelLogin()
{
	if (ActiveLogin.IsValid())
	{
		ActiveLogin->OnProcessRequestComplete().Unbind();
		ActiveLogin->CancelRequest();
		ActiveLogin.Reset();
	}
	LoginStartedSeconds = -1.0;
}

void FRPGBackendAuthentication::CancelLogout()
{
	LogoutContextValidator = nullptr;
	LogoutCompletion = nullptr;
	if (ActiveLogout.IsValid())
	{
		ActiveLogout->OnProcessRequestComplete().Unbind();
		ActiveLogout->CancelRequest();
		ActiveLogout.Reset();
	}
}

void FRPGBackendAuthentication::Authenticate(const FString& SteamTicketHex)
{
	// Duplicate calls must not complete/cancel the existing login.
	if (IsLoginActive() || IsLogoutActive()) return;
	const FHttpRequestPtr Request = CreateLoginRequest(SteamTicketHex);
	if (!Request.IsValid())
	{
		Host.BroadcastAuthenticationResult(false);
		return;
	}
	if (!Request->ProcessRequest() && ActiveLogin == Request)
	{
		ActiveLogin.Reset();
		LoginStartedSeconds = -1.0;
		Host.BroadcastAuthenticationResult(false);
	}
}

FHttpRequestPtr FRPGBackendAuthentication::CreateLoginRequest(const FString& SteamTicketHex)
{
	if (IsLoginActive() || IsLogoutActive() || SteamTicketHex.IsEmpty()) return nullptr;
	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("ticket"), SteamTicketHex);
	FString RequestBody;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
	if (!FJsonSerializer::Serialize(RootObject, Writer)) return nullptr;
	const FRPGBackendAuthenticationConfig Config = Host.GetAuthenticationConfig();
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	const TWeakPtr<FRPGBackendAuthentication> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetAuthenticationOwner(),
		[WeakSelf](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const auto Self = WeakSelf.Pin()) Self->HandleLoginResponse(Completed, Response, bSuccess);
		});
	Request->SetURL(Config.ApiUrl / TEXT("auth/steam-ticket"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Host.ApplyAuthenticationHeaders(Request);
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(Config.TimeoutSeconds);
	ActiveLogin = Request;
	LoginStartedSeconds = FPlatformTime::Seconds();
	return Request;
}

bool FRPGBackendAuthentication::RequestLogout(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion)
{
	const auto Request = CreateLogoutRequest(MoveTemp(CanFinalize), MoveTemp(Completion));
	if (!Request.IsValid()) return false;
	if (!Request->ProcessRequest()) HandleLogoutResponse(Request, nullptr, false);
	return true;
}

FHttpRequestPtr FRPGBackendAuthentication::CreateLogoutRequest(TFunction<bool()> CanFinalize, TFunction<void(bool)> Completion)
{
	if (!IsInGameThread()) return nullptr;
	const FRPGBackendAuthenticationConfig Config = Host.GetAuthenticationConfig();
	if (Config.bServerRuntime || IsLogoutActive() || IsLoginActive()
		|| Config.bProgressionPending || State.LogoutAuthorization().IsEmpty() || !CanFinalize || !Completion) return nullptr;
	const auto Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetURL(Config.ApiUrl / TEXT("auth/logout"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), State.LogoutAuthorization());
	Request->SetTimeout(FMath::Clamp(Config.TimeoutSeconds, 1.f, 30.f));
	const TWeakPtr<FRPGBackendAuthentication> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetAuthenticationOwner(),
		[WeakSelf](FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const auto Self = WeakSelf.Pin()) Self->HandleLogoutResponse(Completed, Response, bSuccess);
		});
	LogoutContextValidator = MoveTemp(CanFinalize);
	LogoutCompletion = MoveTemp(Completion);
	ActiveLogout = Request;
	return Request;
}

void FRPGBackendAuthentication::HandleLogoutResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	const bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActiveLogout) return;
	const bool bSameCredential = State.MatchesRequest(Request->GetHeader(TEXT("Authorization")));
	const bool bConfirmed = bWasSuccessful && Response.IsValid()
		&& (Response->GetResponseCode() == 204 || Response->GetResponseCode() == 401);
	auto ValidateContext = MoveTemp(LogoutContextValidator);
	auto Completion = MoveTemp(LogoutCompletion);
	const bool bSuccess = bSameCredential && bConfirmed && ValidateContext && ValidateContext();
	if (bSuccess)
	{
		State.Reset();
		Host.InvalidateAuthenticationDungeonRequests();
	}
	else if (bSameCredential && bConfirmed)
	{
		// The server may have revoked the token, but a changed world/selection
		// cannot authorize an account switch. Preserve the pinned identity.
		State.Invalidate();
	}
	ActiveLogout.Reset();
	if (Completion) Completion(bSuccess);
}

void FRPGBackendAuthentication::HandleLoginResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
	bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActiveLogin) return;
	Host.ObserveAuthenticationResponse(Request, Response);
	const double RequestStartedSeconds = LoginStartedSeconds;
	ActiveLogin.Reset();
	LoginStartedSeconds = -1.0;
	bool bSuccess = bWasSuccessful && Response.IsValid()
		&& EHttpResponseCodes::IsOk(Response->GetResponseCode());
	if (bSuccess)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		FString NewAccessToken, SteamId, ExpiresAt, IssuedAt;
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& RootObject.IsValid()
			&& RootObject->TryGetStringField(TEXT("accessToken"), NewAccessToken)
			&& RootObject->TryGetStringField(TEXT("steamId"), SteamId)
			&& RootObject->TryGetStringField(TEXT("expiresAt"), ExpiresAt)
			&& RootObject->TryGetStringField(TEXT("issuedAt"), IssuedAt)
			&& State.AcceptLogin(NewAccessToken, SteamId, ExpiresAt, IssuedAt,
				RequestStartedSeconds, FPlatformTime::Seconds());
	}
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("Steam backend authentication failed (HTTP %d)."),
			Response.IsValid() ? Response->GetResponseCode() : 0);
	}
	Host.BroadcastAuthenticationResult(bSuccess);
}
