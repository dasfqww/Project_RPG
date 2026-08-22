#include "Item/Backend/RPGItemBackendTransport.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

FRPGHttpItemBackendTransport::FRPGHttpItemBackendTransport(
	FString InApiUrl,
	FString InBearerToken,
	const float InTimeoutSeconds)
	: ApiUrl(MoveTemp(InApiUrl))
	, BearerToken(MoveTemp(InBearerToken))
	, TimeoutSeconds(FMath::Max(1.0f, InTimeoutSeconds))
{
	ApiUrl = ApiUrl.TrimStartAndEnd();
	ApiUrl.RemoveFromEnd(TEXT("/"));
	BearerToken = BearerToken.TrimStartAndEnd();
}

void FRPGHttpItemBackendTransport::Send(
	const FRPGItemBackendHttpRequest& RequestData,
	FRPGItemBackendHttpCompletion Completion)
{
	if (!Completion)
	{
		return;
	}

	if (ApiUrl.IsEmpty() ||
		BearerToken.IsEmpty() ||
		RequestData.Verb.IsEmpty() ||
		RequestData.RelativePath.IsEmpty())
	{
		FRPGItemBackendHttpResponse Response;
		Completion(MoveTemp(Response));
		return;
	}

	const TSharedRef<FRPGItemBackendHttpCompletion> SharedCompletion =
		MakeShared<FRPGItemBackendHttpCompletion>(MoveTemp(Completion));
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiUrl / RequestData.RelativePath);
	Request->SetVerb(RequestData.Verb);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(
		TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *BearerToken));
	if (!RequestData.Body.IsEmpty())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(RequestData.Body);
	}
	Request->SetTimeout(TimeoutSeconds);
	Request->OnProcessRequestComplete().BindLambda(
		[SharedCompletion](
			FHttpRequestPtr,
			FHttpResponsePtr HttpResponse,
			const bool bWasSuccessful)
		{
			FRPGItemBackendHttpResponse Response;
			Response.bTransportSuccessful =
				bWasSuccessful && HttpResponse.IsValid();
			if (HttpResponse.IsValid())
			{
				Response.StatusCode = HttpResponse->GetResponseCode();
				Response.Body = HttpResponse->GetContentAsString();
			}
			(*SharedCompletion)(MoveTemp(Response));
		});

	if (!Request->ProcessRequest())
	{
		Request->OnProcessRequestComplete().Unbind();
		FRPGItemBackendHttpResponse Response;
		(*SharedCompletion)(MoveTemp(Response));
	}
}
