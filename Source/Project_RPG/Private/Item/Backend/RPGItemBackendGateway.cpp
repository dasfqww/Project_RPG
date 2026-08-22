#include "Item/Backend/RPGItemBackendGateway.h"

#include "GenericPlatform/GenericPlatformHttp.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"

namespace RPGItemBackendGateway
{
const TCHAR* OwnerTypeString(const ERPGItemOwnerType OwnerType)
{
	switch (OwnerType)
	{
	case ERPGItemOwnerType::Character: return TEXT("Character");
	case ERPGItemOwnerType::Account: return TEXT("Account");
	case ERPGItemOwnerType::System: return TEXT("System");
	case ERPGItemOwnerType::World: return TEXT("World");
	default: return TEXT("None");
	}
}

bool IsTransient(const FRPGItemBackendHttpResponse& Response)
{
	return !Response.bTransportSuccessful ||
		Response.StatusCode == 408 ||
		Response.StatusCode == 425 ||
		Response.StatusCode == 429 ||
		Response.StatusCode >= 500;
}

ERPGItemBackendStatus StatusFromHttp(const int32 StatusCode)
{
	switch (StatusCode)
	{
	case 400: return ERPGItemBackendStatus::InvalidRequest;
	case 401: return ERPGItemBackendStatus::Unauthorized;
	case 403: return ERPGItemBackendStatus::Forbidden;
	case 404: return ERPGItemBackendStatus::NotFound;
	default:
		return StatusCode >= 500
			? ERPGItemBackendStatus::ServerError
			: ERPGItemBackendStatus::ProtocolError;
	}
}

FString ResponseError(const FRPGItemBackendHttpResponse& Response)
{
	return Response.Body.IsEmpty()
		? TEXT("The item backend request failed.")
		: Response.Body.Left(512);
}

struct FLoadContext final
	: TSharedFromThis<FLoadContext, ESPMode::ThreadSafe>
{
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport;
	FRPGItemBackendHttpRequest Request;
	int32 MaximumAttempts = 1;
	int32 Attempts = 0;
	FRPGItemBackendLoadCompletion Completion;

	FLoadContext(
		TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> InTransport,
		FRPGItemBackendHttpRequest InRequest,
		const int32 InMaximumAttempts,
		FRPGItemBackendLoadCompletion InCompletion)
		: Transport(MoveTemp(InTransport))
		, Request(MoveTemp(InRequest))
		, MaximumAttempts(InMaximumAttempts)
		, Completion(MoveTemp(InCompletion))
	{
	}

	void Send()
	{
		++Attempts;
		Transport->Send(
			Request,
			[Self = AsShared()](FRPGItemBackendHttpResponse Response)
			{
				Self->Handle(MoveTemp(Response));
			});
	}

	void Handle(FRPGItemBackendHttpResponse Response)
	{
		if (IsTransient(Response) && Attempts < MaximumAttempts)
		{
			Send();
			return;
		}

		FRPGItemBackendLoadResult Result;
		Result.HttpStatusCode = Response.StatusCode;
		if (!Response.bTransportSuccessful)
		{
			Result.Status = ERPGItemBackendStatus::TransportError;
			Result.Error = TEXT("The item backend transport failed.");
		}
		else if (Response.StatusCode >= 200 && Response.StatusCode < 300)
		{
			if (FRPGItemBackendJsonCodec::DeserializeLoadResponse(
				Response.Body,
				Result.Records,
				&Result.Error))
			{
				Result.Status = ERPGItemBackendStatus::Succeeded;
			}
			else
			{
				Result.Status = ERPGItemBackendStatus::ProtocolError;
			}
		}
		else
		{
			Result.Status = StatusFromHttp(Response.StatusCode);
			Result.Error = ResponseError(Response);
		}
		Complete(MoveTemp(Result));
	}

	void Complete(FRPGItemBackendLoadResult Result)
	{
		FRPGItemBackendLoadCompletion Callback = MoveTemp(Completion);
		if (Callback)
		{
			Callback(MoveTemp(Result));
		}
	}
};

struct FCommitContext final
	: TSharedFromThis<FCommitContext, ESPMode::ThreadSafe>
{
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport;
	FRPGItemRepositoryCommitRequest CommitRequest;
	FRPGItemBackendHttpRequest HttpRequest;
	int32 MaximumAttempts = 1;
	int32 Attempts = 0;
	FRPGItemBackendCommitCompletion Completion;

	FCommitContext(
		TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> InTransport,
		FRPGItemRepositoryCommitRequest InCommitRequest,
		FRPGItemBackendHttpRequest InHttpRequest,
		const int32 InMaximumAttempts,
		FRPGItemBackendCommitCompletion InCompletion)
		: Transport(MoveTemp(InTransport))
		, CommitRequest(MoveTemp(InCommitRequest))
		, HttpRequest(MoveTemp(InHttpRequest))
		, MaximumAttempts(InMaximumAttempts)
		, Completion(MoveTemp(InCompletion))
	{
	}

	void Send()
	{
		++Attempts;
		Transport->Send(
			HttpRequest,
			[Self = AsShared()](FRPGItemBackendHttpResponse Response)
			{
				Self->Handle(MoveTemp(Response));
			});
	}

	void Handle(FRPGItemBackendHttpResponse Response)
	{
		if (IsTransient(Response) && Attempts < MaximumAttempts)
		{
			Send();
			return;
		}

		FRPGItemBackendCommitResult Result;
		Result.HttpStatusCode = Response.StatusCode;
		Result.RequestId = CommitRequest.RequestId;
		Result.Operation = CommitRequest.Operation;
		Result.CommandFingerprint = CommitRequest.CommandFingerprint;
		Result.Actor = CommitRequest.Actor;
		if (!Response.bTransportSuccessful)
		{
			Result.Status = ERPGItemBackendStatus::TransportError;
			Result.Error = TEXT("The item backend transport failed.");
			Complete(MoveTemp(Result));
			return;
		}

		FRPGItemBackendCommitResult ParsedResult;
		FString ParseError;
		if (FRPGItemBackendJsonCodec::DeserializeCommitResponse(
			Response.Body,
			ParsedResult,
			&ParseError))
		{
			ParsedResult.HttpStatusCode = Response.StatusCode;
			if (ParsedResult.RequestId != CommitRequest.RequestId ||
				ParsedResult.Operation != CommitRequest.Operation ||
				ParsedResult.CommandFingerprint !=
					CommitRequest.CommandFingerprint ||
				ParsedResult.Actor != CommitRequest.Actor)
			{
				Result.Status = ERPGItemBackendStatus::ProtocolError;
				Result.Error = TEXT(
					"The item backend response does not match the request.");
				Complete(MoveTemp(Result));
				return;
			}
			Complete(MoveTemp(ParsedResult));
			return;
		}

		Result.Status = StatusFromHttp(Response.StatusCode);
		Result.Error = Response.StatusCode >= 200 && Response.StatusCode < 300
			? MoveTemp(ParseError)
			: ResponseError(Response);
		Complete(MoveTemp(Result));
	}

	void Complete(FRPGItemBackendCommitResult Result)
	{
		FRPGItemBackendCommitCompletion Callback = MoveTemp(Completion);
		if (Callback)
		{
			Callback(MoveTemp(Result));
		}
	}
};
}

FRPGItemBackendGateway::FRPGItemBackendGateway(
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> InTransport,
	const int32 InMaximumAttempts)
	: Transport(MoveTemp(InTransport))
	, MaximumAttempts(FMath::Clamp(InMaximumAttempts, 1, 5))
{
}

void FRPGItemBackendGateway::LoadItems(
	const FRPGItemOwnerRef& Owner,
	const bool bIncludeTerminal,
	const int32 Limit,
	FRPGItemBackendLoadCompletion Completion) const
{
	if (!Completion)
	{
		return;
	}
	if (!Owner.IsValid())
	{
		FRPGItemBackendLoadResult Result;
		Result.Status = ERPGItemBackendStatus::InvalidRequest;
		Result.Error = TEXT("The item owner is invalid.");
		Completion(MoveTemp(Result));
		return;
	}

	FRPGItemBackendHttpRequest Request;
	Request.Verb = TEXT("GET");
	Request.RelativePath = FString::Printf(
		TEXT("items?ownerType=%s&ownerId=%s&includeTerminal=%s&limit=%d"),
		RPGItemBackendGateway::OwnerTypeString(Owner.Type),
		*FGenericPlatformHttp::UrlEncode(Owner.OwnerId),
		bIncludeTerminal ? TEXT("true") : TEXT("false"),
		FMath::Clamp(Limit, 1, 500));
	const TSharedRef<RPGItemBackendGateway::FLoadContext, ESPMode::ThreadSafe>
		Context = MakeShared<
			RPGItemBackendGateway::FLoadContext,
			ESPMode::ThreadSafe>(
			Transport,
			MoveTemp(Request),
			MaximumAttempts,
			MoveTemp(Completion));
	Context->Send();
}

void FRPGItemBackendGateway::Commit(
	const FRPGItemRepositoryCommitRequest& Request,
	FRPGItemBackendCommitCompletion Completion) const
{
	if (!Completion)
	{
		return;
	}

	FString Body;
	FString Error;
	if (!FRPGItemBackendJsonCodec::SerializeCommitRequest(
		Request,
		Body,
		&Error))
	{
		FRPGItemBackendCommitResult Result;
		Result.Status = ERPGItemBackendStatus::InvalidRequest;
		Result.RequestId = Request.RequestId;
		Result.Operation = Request.Operation;
		Result.CommandFingerprint = Request.CommandFingerprint;
		Result.Actor = Request.Actor;
		Result.Error = MoveTemp(Error);
		Completion(MoveTemp(Result));
		return;
	}

	FRPGItemBackendHttpRequest HttpRequest;
	HttpRequest.Verb = TEXT("POST");
	HttpRequest.RelativePath = TEXT("item-transactions/commit");
	HttpRequest.Body = MoveTemp(Body);
	const TSharedRef<RPGItemBackendGateway::FCommitContext, ESPMode::ThreadSafe>
		Context = MakeShared<
			RPGItemBackendGateway::FCommitContext,
			ESPMode::ThreadSafe>(
			Transport,
			Request,
			MoveTemp(HttpRequest),
			MaximumAttempts,
			MoveTemp(Completion));
	Context->Send();
}
