#include "Economy/Backend/RPGEconomyBackendGateway.h"

#include "Economy/Backend/RPGEconomyBackendJsonCodec.h"

namespace RPGEconomyBackendGateway
{
bool IsTransient(const FRPGEconomyHttpResponse& Response)
{
	return !Response.bTransportSuccessful ||
		Response.StatusCode == 408 ||
		Response.StatusCode == 425 ||
		Response.StatusCode == 429 ||
		Response.StatusCode >= 500;
}

ERPGEconomyBackendStatus StatusFromHttp(const int32 StatusCode)
{
	switch (StatusCode)
	{
	case 400: return ERPGEconomyBackendStatus::InvalidRequest;
	case 401: return ERPGEconomyBackendStatus::Unauthorized;
	case 403: return ERPGEconomyBackendStatus::Forbidden;
	case 404: return ERPGEconomyBackendStatus::NotFound;
	default:
		return StatusCode >= 500
			? ERPGEconomyBackendStatus::ServerError
			: ERPGEconomyBackendStatus::ProtocolError;
	}
}

FString ResponseError(const FRPGEconomyHttpResponse& Response)
{
	return Response.Body.IsEmpty()
		? TEXT("The economy backend request failed.")
		: Response.Body.Left(512);
}

bool SameGuid(const FString& Left, const FString& Right)
{
	FGuid LeftGuid;
	FGuid RightGuid;
	return FGuid::Parse(Left, LeftGuid) &&
		FGuid::Parse(Right, RightGuid) &&
		LeftGuid == RightGuid;
}

bool ChangesMatch(
	const TArray<FRPGCurrencyChangeResult>& Results,
	const TArray<FRPGCurrencyChange>& Requests)
{
	if (Results.Num() != Requests.Num())
	{
		return false;
	}

	for (const FRPGCurrencyChange& Request : Requests)
	{
		const FRPGCurrencyChangeResult* Result = Results.FindByPredicate(
			[&Request](const FRPGCurrencyChangeResult& Candidate)
			{
				return Candidate.CurrencyCode == Request.CurrencyCode;
			});
		if (!Result || Result->Delta != Request.Delta)
		{
			return false;
		}
	}
	return true;
}

struct FWalletContext final
	: TSharedFromThis<FWalletContext, ESPMode::ThreadSafe>
{
	TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> Transport;
	FRPGEconomyHttpRequest Request;
	FString CharacterId;
	int32 MaximumAttempts = 1;
	int32 Attempts = 0;
	FRPGEconomyWalletCompletion Completion;

	FWalletContext(
		TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> InTransport,
		FRPGEconomyHttpRequest InRequest,
		FString InCharacterId,
		const int32 InMaximumAttempts,
		FRPGEconomyWalletCompletion InCompletion)
		: Transport(MoveTemp(InTransport))
		, Request(MoveTemp(InRequest))
		, CharacterId(MoveTemp(InCharacterId))
		, MaximumAttempts(InMaximumAttempts)
		, Completion(MoveTemp(InCompletion))
	{
	}

	void Send()
	{
		++Attempts;
		Transport->Send(
			Request,
			[Self = AsShared()](FRPGEconomyHttpResponse Response)
			{
				Self->Handle(MoveTemp(Response));
			});
	}

	void Handle(FRPGEconomyHttpResponse Response)
	{
		if (IsTransient(Response) && Attempts < MaximumAttempts)
		{
			Send();
			return;
		}

		FRPGEconomyWalletResult Result;
		Result.HttpStatusCode = Response.StatusCode;
		if (!Response.bTransportSuccessful)
		{
			Result.Status = ERPGEconomyBackendStatus::TransportError;
			Result.Error = TEXT("The economy backend transport failed.");
		}
		else if (Response.StatusCode >= 200 && Response.StatusCode < 300)
		{
			if (FRPGEconomyBackendJsonCodec::DeserializeWalletResponse(
				Response.Body,
				Result,
				&Result.Error) &&
				SameGuid(Result.CharacterId, CharacterId))
			{
				Result.Status = ERPGEconomyBackendStatus::Succeeded;
				Result.HttpStatusCode = Response.StatusCode;
			}
			else
			{
				Result.Status = ERPGEconomyBackendStatus::ProtocolError;
				if (Result.Error.IsEmpty())
				{
					Result.Error = TEXT(
						"The economy wallet belongs to another character.");
				}
			}
		}
		else
		{
			Result.Status = StatusFromHttp(Response.StatusCode);
			Result.Error = ResponseError(Response);
		}
		Complete(MoveTemp(Result));
	}

	void Complete(FRPGEconomyWalletResult Result)
	{
		FRPGEconomyWalletCompletion Callback = MoveTemp(Completion);
		if (Callback)
		{
			Callback(MoveTemp(Result));
		}
	}
};

struct FCommitContext final
	: TSharedFromThis<FCommitContext, ESPMode::ThreadSafe>
{
	TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> Transport;
	FRPGEconomyTransactionRequest CommitRequest;
	FRPGEconomyHttpRequest HttpRequest;
	int32 MaximumAttempts = 1;
	int32 Attempts = 0;
	FRPGEconomyCommitCompletion Completion;

	FCommitContext(
		TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> InTransport,
		FRPGEconomyTransactionRequest InCommitRequest,
		FRPGEconomyHttpRequest InHttpRequest,
		const int32 InMaximumAttempts,
		FRPGEconomyCommitCompletion InCompletion)
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
			[Self = AsShared()](FRPGEconomyHttpResponse Response)
			{
				Self->Handle(MoveTemp(Response));
			});
	}

	void Handle(FRPGEconomyHttpResponse Response)
	{
		if (IsTransient(Response) && Attempts < MaximumAttempts)
		{
			Send();
			return;
		}

		FRPGEconomyCommitResult Result;
		Result.HttpStatusCode = Response.StatusCode;
		Result.RequestId = CommitRequest.RequestId;
		Result.CharacterId = CommitRequest.CharacterId;
		Result.Operation = CommitRequest.Operation;
		Result.CommandFingerprint = CommitRequest.CommandFingerprint;
		Result.Reason = CommitRequest.Reason;
		if (!Response.bTransportSuccessful)
		{
			Result.Status = ERPGEconomyBackendStatus::TransportError;
			Result.Error = TEXT("The economy backend transport failed.");
			Complete(MoveTemp(Result));
			return;
		}

		FRPGEconomyCommitResult ParsedResult;
		FString ParseError;
		if (FRPGEconomyBackendJsonCodec::DeserializeCommitResponse(
			Response.Body,
			ParsedResult,
			&ParseError))
		{
			ParsedResult.HttpStatusCode = Response.StatusCode;
			if (ParsedResult.RequestId != CommitRequest.RequestId ||
				!SameGuid(
					ParsedResult.CharacterId,
					CommitRequest.CharacterId) ||
				ParsedResult.Operation != CommitRequest.Operation ||
				ParsedResult.CommandFingerprint !=
					CommitRequest.CommandFingerprint ||
				ParsedResult.Reason != CommitRequest.Reason ||
				(ParsedResult.WasSuccessful() &&
					!ChangesMatch(
						ParsedResult.Changes,
						CommitRequest.Changes)))
			{
				Result.Status = ERPGEconomyBackendStatus::ProtocolError;
				Result.Error = TEXT(
					"The economy response does not match the request.");
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

	void Complete(FRPGEconomyCommitResult Result)
	{
		FRPGEconomyCommitCompletion Callback = MoveTemp(Completion);
		if (Callback)
		{
			Callback(MoveTemp(Result));
		}
	}
};
}

FRPGEconomyBackendGateway::FRPGEconomyBackendGateway(
	TSharedRef<IRPGEconomyBackendTransport, ESPMode::ThreadSafe> InTransport,
	const int32 InMaximumAttempts)
	: Transport(MoveTemp(InTransport))
	, MaximumAttempts(FMath::Clamp(InMaximumAttempts, 1, 5))
{
}

void FRPGEconomyBackendGateway::LoadWallet(
	const FString& CharacterId,
	const FString& DungeonSessionId,
	FRPGEconomyWalletCompletion Completion) const
{
	if (!Completion)
	{
		return;
	}

	FGuid CharacterGuid;
	FGuid DungeonSessionGuid;
	if (!FGuid::Parse(CharacterId, CharacterGuid) ||
		!FGuid::Parse(DungeonSessionId, DungeonSessionGuid))
	{
		FRPGEconomyWalletResult Result;
		Result.Status = ERPGEconomyBackendStatus::InvalidRequest;
		Result.Error = TEXT("The backend character ID is invalid.");
		Completion(MoveTemp(Result));
		return;
	}

	const FString NormalizedCharacterId = CharacterGuid.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	const FString NormalizedDungeonSessionId = DungeonSessionGuid.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	FRPGEconomyHttpRequest Request;
	Request.Verb = TEXT("GET");
	Request.RelativePath = FString::Printf(
		TEXT("economy/wallets/%s?dungeonSessionId=%s"),
		*NormalizedCharacterId,
		*NormalizedDungeonSessionId);
	const TSharedRef<RPGEconomyBackendGateway::FWalletContext,
		ESPMode::ThreadSafe> Context = MakeShared<
			RPGEconomyBackendGateway::FWalletContext,
			ESPMode::ThreadSafe>(
			Transport,
			MoveTemp(Request),
			NormalizedCharacterId,
			MaximumAttempts,
			MoveTemp(Completion));
	Context->Send();
}

void FRPGEconomyBackendGateway::Commit(
	const FRPGEconomyTransactionRequest& Request,
	FRPGEconomyCommitCompletion Completion) const
{
	if (!Completion)
	{
		return;
	}

	FString Body;
	FString Error;
	if (!FRPGEconomyBackendJsonCodec::SerializeTransactionRequest(
		Request,
		Body,
		&Error))
	{
		FRPGEconomyCommitResult Result;
		Result.Status = ERPGEconomyBackendStatus::InvalidRequest;
		Result.RequestId = Request.RequestId;
		Result.CharacterId = Request.CharacterId;
		Result.Operation = Request.Operation;
		Result.CommandFingerprint = Request.CommandFingerprint;
		Result.Reason = Request.Reason;
		Result.Error = MoveTemp(Error);
		Completion(MoveTemp(Result));
		return;
	}

	FRPGEconomyHttpRequest HttpRequest;
	HttpRequest.Verb = TEXT("POST");
	HttpRequest.RelativePath = TEXT("economy/transactions/commit");
	HttpRequest.Body = MoveTemp(Body);
	const TSharedRef<RPGEconomyBackendGateway::FCommitContext,
		ESPMode::ThreadSafe> Context = MakeShared<
			RPGEconomyBackendGateway::FCommitContext,
			ESPMode::ThreadSafe>(
			Transport,
			Request,
			MoveTemp(HttpRequest),
			MaximumAttempts,
			MoveTemp(Completion));
	Context->Send();
}
