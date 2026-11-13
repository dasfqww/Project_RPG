#include "Online/RPGDungeonSessionTransport.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

namespace
{
	bool TryParseDungeonSession(
		const TSharedPtr<FJsonObject>& Object,
		FRPGDungeonSession& OutSession)
	{
		if (!Object.IsValid()
			|| !Object->TryGetStringField(
				TEXT("dungeonSessionId"), OutSession.DungeonSessionId)
			|| !Object->TryGetStringField(
				TEXT("dungeonId"), OutSession.DungeonId)
			|| !Object->TryGetStringField(
				TEXT("difficulty"), OutSession.Difficulty)
			|| !Object->TryGetStringField(TEXT("state"), OutSession.State)
			|| !Object->TryGetStringField(
				TEXT("expiresAt"), OutSession.ExpiresAt)
			|| OutSession.DungeonSessionId.IsEmpty())
		{
			return false;
		}

		Object->TryGetStringField(TEXT("serverId"), OutSession.ServerId);
		Object->TryGetStringField(
			TEXT("serverAddress"),
			OutSession.ServerAddress);
		// Missing metadata is a read-only legacy session, never implicit leader authority.
		Object->TryGetStringField(TEXT("leaderCharacterId"), OutSession.LeaderCharacterId);
		Object->TryGetStringField(TEXT("departureRequestId"), OutSession.DepartureRequestId);
		Object->TryGetStringField(TEXT("departureRequestedAt"), OutSession.DepartureRequestedAt);
		double Revision = 0, RequestRevision = 0;
		if (Object->HasField(TEXT("lobbyRevision")) && (!Object->TryGetNumberField(TEXT("lobbyRevision"), Revision)
			|| !FMath::IsFinite(Revision) || Revision < 0 || Revision > 9007199254740991.0 || FMath::FloorToDouble(Revision) != Revision)) return false;
		if (Object->TryGetNumberField(TEXT("departureRequestRevision"), RequestRevision)
			&& (!FMath::IsFinite(RequestRevision) || RequestRevision < 1 || RequestRevision > 9007199254740991.0
				|| FMath::FloorToDouble(RequestRevision) != RequestRevision)) return false;
		OutSession.LobbyRevision = static_cast<int64>(Revision);
		OutSession.DepartureRequestRevision = static_cast<int64>(RequestRevision);
		FGuid Leader, Request;
		if ((!OutSession.LeaderCharacterId.IsEmpty() && !FGuid::Parse(OutSession.LeaderCharacterId, Leader))
			|| (!OutSession.DepartureRequestId.IsEmpty() && (!FGuid::Parse(OutSession.DepartureRequestId, Request) || RequestRevision < 1))
			|| (!OutSession.DepartureRequestedAt.IsEmpty() && OutSession.DepartureRequestId.IsEmpty())) return false;

		const TArray<TSharedPtr<FJsonValue>>* MemberValues = nullptr;
		if (!Object->TryGetArrayField(TEXT("members"), MemberValues)
			|| !MemberValues)
		{
			return false;
		}

		OutSession.Members.Reset(MemberValues->Num());
		for (const TSharedPtr<FJsonValue>& MemberValue : *MemberValues)
		{
			const TSharedPtr<FJsonObject> MemberObject =
				MemberValue.IsValid() ? MemberValue->AsObject() : nullptr;
			FRPGDungeonSessionMember Member;
			if (!MemberObject.IsValid()
				|| !MemberObject->TryGetStringField(
					TEXT("characterId"), Member.CharacterId)
				|| !MemberObject->TryGetStringField(
					TEXT("joinedAt"), Member.JoinedAt)
				|| !MemberObject->TryGetStringField(
					TEXT("leaseExpiresAt"), Member.LeaseExpiresAt)
				|| Member.CharacterId.IsEmpty())
			{
				OutSession = FRPGDungeonSession();
				return false;
			}

			// Optional display metadata. Missing/null/invalid-type names must not reject valid legacy membership.
			if (MemberObject->HasTypedField<EJson::String>(TEXT("characterName")))
				MemberObject->TryGetStringField(TEXT("characterName"), Member.CharacterName);
			OutSession.Members.Add(MoveTemp(Member));
		}

		return true;
	}
}

void FRPGDungeonSessionTransport::Create(
	const FString& CharacterId,
	const FString& DungeonId,
	const FString& Difficulty)
{
	const FString NormalizedDungeonId = DungeonId.TrimStartAndEnd();
	const FString NormalizedDifficulty = Difficulty.TrimStartAndEnd();
	if (!Host.HasSessionAccessToken()
		|| CharacterId.IsEmpty()
		|| NormalizedDungeonId.IsEmpty()
		|| NormalizedDifficulty.IsEmpty())
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	RootObject->SetStringField(TEXT("dungeonId"), NormalizedDungeonId);
	RootObject->SetStringField(TEXT("difficulty"), NormalizedDifficulty);
	SendRequest(
		TEXT("dungeon-sessions"),
		TEXT("POST"),
		RootObject,
		TEXT("create"),
		FString());
}

void FRPGDungeonSessionTransport::Join(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (!Host.HasSessionAccessToken()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("characterId"), CharacterId);
	SendRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("POST"),
		RootObject,
		TEXT("join"),
		DungeonSessionId);
}

void FRPGDungeonSessionTransport::Load(const FString& DungeonSessionId)
{
	if (!Host.HasSessionAccessToken() || DungeonSessionId.IsEmpty())
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	SendRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId)),
		TEXT("GET"),
		nullptr,
		TEXT("load"),
		DungeonSessionId);
}

void FRPGDungeonSessionTransport::LoadActive(
	const FString& CharacterId)
{
	const FString NormalizedCharacterId = CharacterId.TrimStartAndEnd();
	if (!Host.HasSessionAccessToken() || NormalizedCharacterId.IsEmpty())
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	SendRequest(
		FString::Printf(
			TEXT("characters/%s/active-dungeon-session"),
			*FGenericPlatformHttp::UrlEncode(NormalizedCharacterId)),
		TEXT("GET"),
		nullptr,
		TEXT("resume"),
		FString());
}

void FRPGDungeonSessionTransport::Leave(
	const FString& DungeonSessionId,
	const FString& CharacterId)
{
	if (!Host.HasSessionAccessToken()
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty())
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	SendRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/members/%s"),
			*FGenericPlatformHttp::UrlEncode(DungeonSessionId),
			*FGenericPlatformHttp::UrlEncode(CharacterId)),
		TEXT("DELETE"),
		nullptr,
		TEXT("leave"),
		DungeonSessionId);
}

void FRPGDungeonSessionTransport::SetDeparture(const FString& SessionId, const FString& CharacterId,
	const FString& RequestId, int64 ExpectedRevision, bool bCancel)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("characterId"), CharacterId);
	Body->SetStringField(TEXT("requestId"), RequestId);
	Body->SetNumberField(TEXT("expectedLobbyRevision"), static_cast<double>(ExpectedRevision));
	SendRequest(FString::Printf(TEXT("dungeon-sessions/%s/departure%s"),
		*FGenericPlatformHttp::UrlEncode(SessionId), bCancel ? TEXT("/cancel") : TEXT("")),
		TEXT("POST"), Body, TEXT("departure"), SessionId);
}

void FRPGDungeonSessionTransport::SendConfigured(
	const FString& Action,
	const FString& Outcome)
{
	const auto Config = Host.GetSessionTransportConfig();
	if (!Config.bServerRuntime || !Config.bServerConfigured)
	{
		Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
		return;
	}

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("serverId"), Config.ServerId);
	if (!Outcome.IsEmpty())
	{
		RootObject->SetStringField(TEXT("outcome"), Outcome);
	}

	SendRequest(
		FString::Printf(
			TEXT("dungeon-sessions/%s/%s"),
			*FGenericPlatformHttp::UrlEncode(Config.SessionId),
			*Action),
		TEXT("POST"),
		RootObject,
		Action,
		Config.SessionId);
}

void FRPGDungeonSessionTransport::SendRequest(
	const FString& RelativePath,
	const FString& Verb,
	const TSharedPtr<FJsonObject>& RequestObject,
	const FString& Operation,
	const FString& ExpectedDungeonSessionId)
{
	FString RequestBody;
	if (RequestObject.IsValid())
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>>
			Writer = TJsonWriterFactory<
				TCHAR,
				TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestBody);
		if (!FJsonSerializer::Serialize(RequestObject.ToSharedRef(), Writer))
		{
			Host.BroadcastSessionTransportResult(FRPGDungeonSession(), false);
			return;
		}
	}

	const auto Config = Host.GetSessionTransportConfig();
	const FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	const TWeakPtr<FRPGDungeonSessionTransport> WeakSelf = AsShared();
	Request->OnProcessRequestComplete().BindWeakLambda(&Host.GetSessionTransportOwner(),
		[WeakSelf, Operation, ExpectedDungeonSessionId, Generation = Config.PlayerGeneration]
		(FHttpRequestPtr Completed, FHttpResponsePtr Response, bool bSuccess)
		{
			if (const auto Self = WeakSelf.Pin())
				Self->HandleResponse(Completed, Response, bSuccess, Operation, ExpectedDungeonSessionId, Generation);
		});
	Request->SetURL(Config.ApiUrl / RelativePath);
	Request->SetVerb(Verb);
	if (RequestObject.IsValid())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(RequestBody);
	}
	Host.ApplySessionTransportHeaders(Request);
	Request->SetTimeout(Config.TimeoutSeconds);
	Host.DispatchSessionTransportRequest(Request);
}

void FRPGDungeonSessionTransport::HandleResponse(
	FHttpRequestPtr Request,
	FHttpResponsePtr Response,
	bool bWasSuccessful,
	const FString& Operation,
	const FString& ExpectedDungeonSessionId,
	uint64 RequestGeneration)
{
	const auto Config = Host.GetSessionTransportConfig();
	if (!Config.bServerRuntime && RequestGeneration != Config.PlayerGeneration) return;
	if (!Host.AcceptSessionPlayerResponse(Request, Response, bWasSuccessful)) return;
	FRPGDungeonSession DungeonSession;
	const int32 ResponseCode = Response.IsValid()
		? Response->GetResponseCode()
		: 0;
	const bool bNoActiveSession = bWasSuccessful
		&& ResponseCode == EHttpResponseCodes::NoContent
		&& Operation.Equals(TEXT("resume"), ESearchCase::CaseSensitive);
	bool bSuccess = bNoActiveSession
		|| (bWasSuccessful && Response.IsValid()
			&& EHttpResponseCodes::IsOk(ResponseCode));

	if (bSuccess && !bNoActiveSession)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader =
			TJsonReaderFactory<>::Create(Response->GetContentAsString());
		bSuccess = FJsonSerializer::Deserialize(Reader, RootObject)
			&& TryParseDungeonSession(RootObject, DungeonSession)
			&& (ExpectedDungeonSessionId.IsEmpty()
				|| DungeonSession.DungeonSessionId.Equals(
					ExpectedDungeonSessionId,
					ESearchCase::CaseSensitive));
	}

	if (!bSuccess)
	{
		DungeonSession = FRPGDungeonSession();
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon session %s request failed (HTTP %d)."),
			*Operation,
			ResponseCode);
	}

	Host.BroadcastSessionTransportResult(DungeonSession, bSuccess);
}
