#include "Item/Backend/RPGItemBackendGateway.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Item/Backend/RPGItemBackendJsonCodec.h"
#include "Item/Transaction/RPGPickupGrantReceiptPolicy.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGPickupGateway
{
FString Id(const FGuid& Value) { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
bool WriteJson(const TSharedRef<FJsonObject>& Object, FString& Json)
{
	return FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
}
bool ReadGuid(const FJsonObject& Object, const TCHAR* Key, const FGuid& Expected)
{
	FString Text; FGuid Value;
	return Object.TryGetStringField(Key, Text) && FGuid::Parse(Text, Value) && Value.IsValid() && Value == Expected;
}
ERPGItemBackendStatus FailureStatus(const int32 Code)
{
	switch (Code)
	{
	case 400: return ERPGItemBackendStatus::InvalidRequest;
	case 401: return ERPGItemBackendStatus::Unauthorized;
	case 403: return ERPGItemBackendStatus::Forbidden;
	case 404: return ERPGItemBackendStatus::NotFound;
	case 409: return ERPGItemBackendStatus::IdempotencyConflict;
	default: return Code >= 500 ? ERPGItemBackendStatus::ServerError : ERPGItemBackendStatus::ProtocolError;
	}
}

struct FContext final : TSharedFromThis<FContext, ESPMode::ThreadSafe>
{
	TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> Transport;
	FRPGItemRepositoryCommitRequest Frozen;
	FString Server, FrozenBody, IntentPath;
	FGuid Session, Drop;
	FRPGItemBackendHttpRequest Http;
	FRPGItemBackendCommitCompletion Completion;
	TFunction<bool()> CanContinue;
	int32 MaximumAttempts, Attempts = 0;
	bool bReadOnly, bReceiptStage = false;

	FContext(TSharedRef<IRPGItemBackendTransport, ESPMode::ThreadSafe> InTransport,
		const FString& InServer, const FGuid& InSession, const FGuid& InDrop,
		const FRPGItemRepositoryCommitRequest& Request, const int32 InAttempts,
		const bool InReadOnly, FRPGItemBackendCommitCompletion Callback, TFunction<bool()> Guard)
		: Transport(InTransport), Frozen(Request), Server(InServer), Session(InSession), Drop(InDrop),
		Completion(MoveTemp(Callback)), CanContinue(MoveTemp(Guard)), MaximumAttempts(InAttempts), bReadOnly(InReadOnly) {}

	void Complete(ERPGItemBackendStatus Status, const int32 Code = 0)
	{
		FRPGItemBackendCommitResult Result;
		Result.Status = Status; Result.HttpStatusCode = Code;
		Result.RequestId = Frozen.RequestId; Result.Operation = Frozen.Operation;
		Result.CommandFingerprint = Frozen.CommandFingerprint; Result.Actor = Frozen.Actor;
		Result.Error = TEXT("Prepared pickup request did not complete; original reservation retained.");
		auto Callback = MoveTemp(Completion);
		if (Callback) Callback(MoveTemp(Result));
	}
	void Start()
	{
		if (!Session.IsValid() || !Drop.IsValid() || Server.IsEmpty() || Server.Len() > 128 ||
			Server != Server.TrimStartAndEnd() || Frozen.Operation != FName(TEXT("Item.Pickup")) ||
			Frozen.Mutations.Num() != 1 || !FRPGItemBackendJsonCodec::SerializeCommitRequest(Frozen, FrozenBody))
		{ Complete(ERPGItemBackendStatus::InvalidRequest); return; }
		IntentPath = TEXT("dungeon-sessions/") + Id(Session) + TEXT("/pickup-intents");
		if (bReadOnly)
		{
			Http.Verb = TEXT("GET"); Http.RelativePath = IntentPath + TEXT("/") + Id(Frozen.RequestId);
		}
		else
		{
			TSharedPtr<FJsonObject> Command;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FrozenBody), Command) || !Command)
			{ Complete(ERPGItemBackendStatus::InvalidRequest); return; }
			const auto Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("dropId"), Id(Drop)); Body->SetObjectField(TEXT("command"), Command);
			Http.Verb = TEXT("POST"); Http.RelativePath = IntentPath;
			if (!WriteJson(Body, Http.Body)) { Complete(ERPGItemBackendStatus::InvalidRequest); return; }
		}
		Send();
	}
	void Send()
	{
		if (CanContinue && !CanContinue()) { Complete(ERPGItemBackendStatus::Unavailable); return; }
		++Attempts;
		Transport->Send(Http, [Self = AsShared()](FRPGItemBackendHttpResponse Response) mutable
		{
			// Scope/identity guards touch UObjects; every stage transition is on the game thread.
			if (IsInGameThread()) Self->Handle(MoveTemp(Response));
			else AsyncTask(ENamedThreads::GameThread, [Self, Response = MoveTemp(Response)]() mutable { Self->Handle(MoveTemp(Response)); });
		});
	}
	void Handle(FRPGItemBackendHttpResponse Response)
	{
		if (!Completion) return;
		if ((!Response.bTransportSuccessful || Response.StatusCode == 408 || Response.StatusCode == 425 ||
			Response.StatusCode == 429 || Response.StatusCode >= 500) && Attempts < MaximumAttempts)
		{ Send(); return; }
		if (!Response.bTransportSuccessful) { Complete(ERPGItemBackendStatus::TransportError); return; }
		if (Response.StatusCode != 200)
		{
			auto Status = FailureStatus(Response.StatusCode);
			if (bReceiptStage && !bReadOnly && Response.StatusCode == 409)
			{
				TSharedPtr<FJsonObject> Error; FString Name;
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Error) && Error && Error->TryGetStringField(TEXT("status"), Name))
				{
					if (Name == TEXT("LocationConflict")) Status = ERPGItemBackendStatus::LocationConflict;
					else if (Name == TEXT("RevisionConflict")) Status = ERPGItemBackendStatus::RevisionConflict;
				}
			}
			Complete(Status, Response.StatusCode); return;
		}
		if (Response.Body.Len() > 128 * 1024) { Complete(ERPGItemBackendStatus::ProtocolError, 200); return; }
		if (bReceiptStage)
		{
			FRPGItemBackendCommitResult Receipt;
			if (!FRPGItemBackendJsonCodec::DeserializeCommitResponse(Response.Body, Receipt) ||
				FRPGPickupGrantReceiptPolicy::Evaluate(Frozen, Receipt, true) != ERPGPickupGrantReceiptDecision::ApplyReceipt)
			{ Complete(ERPGItemBackendStatus::ProtocolError, 200); return; }
			Receipt.HttpStatusCode = 200;
			auto Callback = MoveTemp(Completion); if (Callback) Callback(MoveTemp(Receipt));
			return;
		}
		TSharedPtr<FJsonObject> Origin; FString Status, StoredServer, Prepared, Consumed;
		const TSharedPtr<FJsonObject>* Command = nullptr;
		FDateTime At;
		FGuid Character;
		if (!FGuid::Parse(Frozen.Actor.OwnerId, Character) ||
			!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response.Body), Origin) || !Origin ||
			!Origin->TryGetStringField(TEXT("status"), Status) || (Status != TEXT("Prepared") && Status != TEXT("AlreadyPrepared")) ||
			!Origin->TryGetStringField(TEXT("serverId"), StoredServer) || StoredServer != Server ||
			!ReadGuid(*Origin, TEXT("dungeonSessionId"), Session) || !ReadGuid(*Origin, TEXT("dropId"), Drop) ||
			!ReadGuid(*Origin, TEXT("requestId"), Frozen.RequestId) || !ReadGuid(*Origin, TEXT("characterId"), Character) ||
			!ReadGuid(*Origin, TEXT("itemId"), Frozen.Mutations[0].NewRecord.GetItemId()) ||
			!Origin->TryGetStringField(TEXT("preparedAt"), Prepared) || !FDateTime::ParseIso8601(*Prepared, At) ||
			!Origin->TryGetObjectField(TEXT("command"), Command) || !Command || !Command->IsValid())
		{ Complete(ERPGItemBackendStatus::ProtocolError, 200); return; }
		FString StoredBody, Canonical; FRPGItemRepositoryCommitRequest Restored;
		if (!WriteJson(Command->ToSharedRef(), StoredBody) || !FRPGItemBackendJsonCodec::DeserializeCommitRequest(StoredBody, Restored) ||
			!FRPGItemBackendJsonCodec::SerializeCommitRequest(Restored, Canonical) || Canonical != FrozenBody)
		{ Complete(ERPGItemBackendStatus::ProtocolError, 200); return; }
		const auto* ConsumedValue = Origin->Values.Find(TEXT("consumedAt"));
		if (!ConsumedValue || !ConsumedValue->IsValid() || ((*ConsumedValue)->Type != EJson::Null &&
			(!Origin->TryGetStringField(TEXT("consumedAt"), Consumed) || !FDateTime::ParseIso8601(*Consumed, At))))
		{ Complete(ERPGItemBackendStatus::ProtocolError, 200); return; }
		if (bReadOnly && (*ConsumedValue)->Type == EJson::Null)
		{ Complete(ERPGItemBackendStatus::Unavailable, 200); return; }
		bReceiptStage = true; Attempts = 0; Http.Body.Reset();
		Http.Verb = bReadOnly ? TEXT("GET") : TEXT("POST");
		Http.RelativePath = bReadOnly ? TEXT("item-transactions/") + Id(Frozen.RequestId)
			: IntentPath + TEXT("/") + Id(Frozen.RequestId) + TEXT("/commit");
		Send();
	}
};
}

void FRPGItemBackendGateway::CommitPickup(const FString& ServerId, const FGuid& SessionId, const FGuid& DropId,
	const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion, TFunction<bool()> CanContinue) const
{
	if (!Completion) return;
	const auto Context = MakeShared<RPGPickupGateway::FContext, ESPMode::ThreadSafe>(Transport, ServerId, SessionId, DropId,
		Request, MaximumAttempts, false, MoveTemp(Completion), MoveTemp(CanContinue));
	Context->Start();
}

void FRPGItemBackendGateway::LoadPickupReceipt(const FString& ServerId, const FGuid& SessionId, const FGuid& DropId,
	const FRPGItemRepositoryCommitRequest& Request, FRPGItemBackendCommitCompletion Completion, TFunction<bool()> CanContinue) const
{
	if (!Completion) return;
	const auto Context = MakeShared<RPGPickupGateway::FContext, ESPMode::ThreadSafe>(Transport, ServerId, SessionId, DropId,
		Request, MaximumAttempts, true, MoveTemp(Completion), MoveTemp(CanContinue));
	Context->Start();
}
