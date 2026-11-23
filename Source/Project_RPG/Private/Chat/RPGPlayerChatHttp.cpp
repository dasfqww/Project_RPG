#include "Manager/HttpWebManager.h"
#include "HAL/PlatformTime.h"

FHttpRequestPtr UHttpWebManager::CreatePlayerChatRequest(FGuid Character, bool bLobby, FGuid Epoch,
	int64 After, const FString& PostBody, TFunction<void(int32, FString)> Complete)
{
	FString Endpoint, Account;
	if (!Character.IsValid() || !GetPlayerEquipmentContext(Endpoint, Account) || !Complete) return nullptr;
	const FString Authorization = PlayerAuth.Authorization(FPlatformTime::Seconds());
	if (Authorization.IsEmpty()) return nullptr;
	auto Request = FHttpModule::Get().CreateRequest();
	FString Url = Endpoint / TEXT("characters") / Character.ToString(EGuidFormats::DigitsWithHyphensLower) / TEXT("chat");
	if (PostBody.IsEmpty())
	{
		Url += FString::Printf(TEXT("?lobby=%s&after=%lld"), bLobby ? TEXT("true") : TEXT("false"), After);
		if (Epoch.IsValid()) Url += TEXT("&epoch=") + Epoch.ToString(EGuidFormats::DigitsWithHyphensLower);
		Request->SetVerb(TEXT("GET"));
	}
	else
	{
		Request->SetVerb(TEXT("POST"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(PostBody);
	}
	Request->SetURL(Url);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), Authorization);
	Request->SetTimeout(FMath::Clamp(RequestTimeoutSeconds, 1.f, 30.f));
	Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
	Request->OnProcessRequestComplete().BindWeakLambda(this,
		[this, Endpoint, Account, Complete = MoveTemp(Complete)](FHttpRequestPtr Req, FHttpResponsePtr Res, bool bOk)
		{
			FString CurrentEndpoint, CurrentAccount;
			if (!GetPlayerEquipmentContext(CurrentEndpoint, CurrentAccount) || CurrentEndpoint != Endpoint
				|| CurrentAccount != Account || !AcceptPlayerResponse(Req, Res, bOk) || !bOk || !Res)
			{
				Complete(0, {});
				return;
			}
			Complete(Res->GetResponseCode(), Res->GetContent().Num() <= 256 * 1024 ? Res->GetContentAsString() : FString());
		});
	return Request;
}
