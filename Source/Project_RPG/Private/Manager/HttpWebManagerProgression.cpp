#include "Manager/HttpWebManager.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Progression/RPGCharacterProgressionSnapshot.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool UHttpWebManager::LoadConfiguredCharacterProgression(const FString& CharacterId,
	TFunction<void(FRPGCharacterProgressionResult)> Completion)
{
	FGuid CharacterGuid;
	if (!Completion || !IsConfiguredForDungeonServer() || !FGuid::Parse(CharacterId, CharacterGuid)
		|| !CharacterGuid.IsValid()) return false;
	const FString ExpectedCharacterId = CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString ExpectedSessionId = ConfiguredDungeonSessionId;
	const FString ExpectedServerId = GameServerId;
	const FString ExpectedCredential = BackendServiceToken;
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	TWeakObjectPtr<UHttpWebManager> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, ExpectedCharacterId, ExpectedSessionId, ExpectedServerId, ExpectedCredential,
		 Completion = MoveTemp(Completion)](FHttpRequestPtr, FHttpResponsePtr Response, const bool bSuccess) mutable
		{
			FRPGCharacterProgressionResult Result;
			Result.HttpStatus = Response ? Response->GetResponseCode() : 0;
			const UHttpWebManager* Manager = WeakThis.Get();
			if (!Manager || !Manager->IsConfiguredForDungeonServer()
				|| Manager->ConfiguredDungeonSessionId != ExpectedSessionId || Manager->GameServerId != ExpectedServerId
				|| Manager->BackendServiceToken != ExpectedCredential)
				Result.Error = TEXT("The progression request no longer belongs to this server session.");
			else if (!bSuccess || !Response || Result.HttpStatus != 200)
				Result.Error = FString::Printf(TEXT("Progression read failed (HTTP %d)."), Result.HttpStatus);
			else
				Result.bSuccessful = FRPGCharacterProgressionSnapshotCodec::Decode(Response->GetContentAsString(),
					ExpectedCharacterId, Result.Snapshot, Result.Error);
			Completion(MoveTemp(Result));
		});
	Request->SetURL(ApiUrl / FString::Printf(TEXT("characters/%s/progression"), *ExpectedCharacterId));
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BackendServiceToken));
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds) ? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	return Request->ProcessRequest();
}

bool UHttpWebManager::LoadPlayerCharacterProgression(const FString& CharacterId,
	TFunction<void(FRPGCharacterProgressionResult)> Completion)
{
	return RequestPlayerProgression(CharacterId, nullptr, MoveTemp(Completion));
}

bool UHttpWebManager::SavePlayerCharacterConfiguration(const FString& CharacterId,
	const FRPGCharacterSkillConfiguration& Command, TFunction<void(FRPGCharacterProgressionResult)> Completion)
{
	return RequestPlayerProgression(CharacterId, &Command, MoveTemp(Completion));
}

bool UHttpWebManager::RequestPlayerProgression(const FString& CharacterId, const FRPGCharacterSkillConfiguration* Command,
	TFunction<void(FRPGCharacterProgressionResult)> Completion)
{
	FGuid CharacterGuid;
	FString Body;
	if (!IsInGameThread() || IsRunningDedicatedServer() || IsConfiguredForDungeonServer() || !Completion
		|| ActivePlayerProgressionRequest.IsValid() || PlayerAuthentication->IsLoginActive() || !HasAccessToken()
		|| !FRPGBackendPlayerAuthState::IsSteamId(GetAuthenticatedSteamId())
		|| !FGuid::Parse(CharacterId, CharacterGuid) || !CharacterGuid.IsValid()
		|| (Command && !FRPGCharacterProgressionSnapshotCodec::EncodeConfiguration(*Command, Body))) return false;
	const FString ExpectedCharacterId = CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
	const FString ExpectedSteamId = GetAuthenticatedSteamId(), ExpectedEndpoint = ApiUrl;
	const bool bWrite = Command != nullptr;
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->SetURL(ApiUrl / FString::Printf(TEXT("characters/%s/%s"), *ExpectedCharacterId,
		bWrite ? TEXT("skill-configuration") : TEXT("progression")));
	Request->SetVerb(bWrite ? TEXT("PUT") : TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	// Deliberately bypass ApplyCommonHeaders: player reads/writes never use a service token.
	Request->SetHeader(TEXT("Authorization"), PlayerAuth.Authorization(FPlatformTime::Seconds()));
	Request->SetTimeout(FMath::IsFinite(RequestTimeoutSeconds) ? FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f) : 10.f);
	if (bWrite)
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(Body);
	}
	const TWeakObjectPtr<UHttpWebManager> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, ExpectedCharacterId, ExpectedSteamId, ExpectedEndpoint, bWrite, Completion = MoveTemp(Completion)]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess) mutable
		{
			auto* Manager = WeakThis.Get();
			if (!Manager || Manager->ActivePlayerProgressionRequest != Completed) return;
			Manager->ActivePlayerProgressionRequest.Reset();
			FRPGCharacterProgressionResult Result;
			Result.HttpStatus = Response ? Response->GetResponseCode() : 0;
			Result.bOutcomeUnknown = bWrite && (!bSuccess || !Response || Result.HttpStatus >= 500);
			if (Manager->ApiUrl != ExpectedEndpoint || Manager->GetAuthenticatedSteamId() != ExpectedSteamId
				|| !Manager->PlayerAuth.MatchesRequest(Completed->GetHeader(TEXT("Authorization"))))
			{
				Result.Error = TEXT("progression_context_changed");
				Result.bOutcomeUnknown = bWrite;
			}
			else
			{
				Manager->PlayerAuth.InvalidateIfUnauthorized(Completed->GetHeader(TEXT("Authorization")), Result.HttpStatus);
				if (bSuccess && Response && Result.HttpStatus == 200)
				{
					Result.bSuccessful = FRPGCharacterProgressionSnapshotCodec::Decode(Response->GetContentAsString(),
						ExpectedCharacterId, Result.Snapshot, Result.Error);
					Result.bOutcomeUnknown = bWrite && !Result.bSuccessful;
				}
				else
				{
					Result.Error = TEXT("progression_request_failed");
					TSharedPtr<FJsonObject> Error;
					FString Code;
					const FString ErrorBody = Response ? Response->GetContentAsString() : FString();
					if (ErrorBody.Len() <= 8192
						&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ErrorBody), Error)
						&& Error && Error->TryGetStringField(TEXT("error"), Code) && Code.Len() <= 96)
						Result.Error = MoveTemp(Code);
				}
			}
			Completion(MoveTemp(Result));
		});
	ActivePlayerProgressionRequest = Request;
	if (!Request->ProcessRequest())
	{
		// Match the asynchronous completion contract even when dispatch fails immediately.
		Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
	}
	return true;
}
