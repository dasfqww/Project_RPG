#include "Online/RPGJoinTicketAdmission.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool DecodeAdmissionFields(const TSharedPtr<FJsonObject>& Object, FRPGJoinTicketAdmission& OutAdmission)
	{
		if (!Object) return false;
		FRPGJoinTicketAdmission Parsed;
		const TSharedPtr<FJsonValue> Generation = Object->TryGetField(TEXT("connectionGeneration"));
		FString GenerationText;
		if (!Object->TryGetStringField(TEXT("dungeonSessionId"), Parsed.DungeonSessionId)
			|| !Object->TryGetStringField(TEXT("characterId"), Parsed.CharacterId)
			|| !Object->TryGetStringField(TEXT("steamId"), Parsed.SteamId)
			|| !Generation.IsValid() || Generation->Type != EJson::Number
			|| !Generation->TryGetString(GenerationText)
			|| GenerationText.IsEmpty() || GenerationText.Len() > 19
			|| !Object->TryGetNumberField(TEXT("connectionGeneration"), Parsed.ConnectionGeneration)
			|| Parsed.ConnectionGeneration <= 0
			|| GenerationText != LexToString(Parsed.ConnectionGeneration)) return false;
		for (const TCHAR Character : GenerationText)
			if (Character < TEXT('0') || Character > TEXT('9')) return false;

		FGuid SessionGuid;
		FGuid CharacterGuid;
		if (!FGuid::Parse(Parsed.DungeonSessionId, SessionGuid) || !SessionGuid.IsValid()
			|| !FGuid::Parse(Parsed.CharacterId, CharacterGuid) || !CharacterGuid.IsValid()
			|| !Parsed.DungeonSessionId.Equals(SessionGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
			|| !Parsed.CharacterId.Equals(CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
			|| Parsed.SteamId.IsEmpty() || Parsed.SteamId.Len() > 20) return false;
		for (const TCHAR Character : Parsed.SteamId)
			if (Character < TEXT('0') || Character > TEXT('9')) return false;

		OutAdmission = MoveTemp(Parsed);
		return true;
	}
}

bool FRPGJoinTicketAdmission::Decode(const FString& Json, FRPGJoinTicketAdmission& OutAdmission)
{
	OutAdmission = FRPGJoinTicketAdmission();
	if (Json.IsEmpty() || Json.Len() > 8192) return false;
	TSharedPtr<FJsonObject> Object;
	// Preserve numeric lexemes, including int64 values beyond double precision.
	return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object,
		FJsonSerializer::EFlags::StoreNumbersAsStrings) && DecodeAdmissionFields(Object, OutAdmission);
}

bool FRPGJoinTicketConsumptionEvidence::Decode(const FString& Json, FRPGJoinTicketConsumptionEvidence& OutEvidence)
{
	OutEvidence = FRPGJoinTicketConsumptionEvidence();
	if (Json.IsEmpty() || Json.Len() > 8192) return false;
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object,
		FJsonSerializer::EFlags::StoreNumbersAsStrings) || !Object) return false;
	FString State, ConsumedAt;
	const auto Cancellation = Object->TryGetField(TEXT("cancellation"));
	if (Cancellation && Cancellation->Type != EJson::Null) return false; // Contradictory branches are not evidence.
	const TSharedPtr<FJsonObject>* Consumption = nullptr;
	FRPGJoinTicketConsumptionEvidence Parsed;
	FGuid RequestGuid;
	if (!Object->TryGetStringField(TEXT("state"), State) || !State.Equals(TEXT("Recorded"), ESearchCase::CaseSensitive)
		|| !Object->TryGetObjectField(TEXT("consumption"), Consumption) || !Consumption
		|| !DecodeAdmissionFields(*Consumption, Parsed.Consumption)
		|| !(*Consumption)->TryGetStringField(TEXT("serverId"), Parsed.ServerId) || Parsed.ServerId.IsEmpty()
		|| Parsed.ServerId.Len() > 128
		|| !(*Consumption)->TryGetStringField(TEXT("consumptionRequestId"), Parsed.ConsumptionRequestId)
		|| !FGuid::ParseExact(Parsed.ConsumptionRequestId, EGuidFormats::DigitsWithHyphensLower, RequestGuid)
		|| !RequestGuid.IsValid()
		|| !Parsed.ConsumptionRequestId.Equals(RequestGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
		|| !(*Consumption)->TryGetStringField(TEXT("consumedAt"), ConsumedAt)
		|| (!ConsumedAt.EndsWith(TEXT("Z"), ESearchCase::CaseSensitive)
			&& !ConsumedAt.EndsWith(TEXT("+00:00"), ESearchCase::CaseSensitive))
		|| !FDateTime::ParseIso8601(*ConsumedAt, Parsed.ConsumedAtUtc)
		|| Parsed.ConsumedAtUtc.GetTicks() <= 0) return false;
	OutEvidence = MoveTemp(Parsed);
	return true;
}

bool FRPGJoinTicketConsumptionCancellation::Decode(const FString& Json, FRPGJoinTicketConsumptionCancellation& OutCancellation)
{
	OutCancellation = FRPGJoinTicketConsumptionCancellation();
	if (Json.IsEmpty() || Json.Len() > 8192) return false;
	TSharedPtr<FJsonObject> Object;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object,
		FJsonSerializer::EFlags::StoreNumbersAsStrings) || !Object) return false;
	FString State, CancelledAt;
	const TSharedPtr<FJsonObject>* Cancellation = nullptr;
	const auto Consumption = Object->TryGetField(TEXT("consumption"));
	FRPGJoinTicketConsumptionCancellation Parsed;
	FGuid SessionGuid, RequestGuid;
	if (!Object->TryGetStringField(TEXT("state"), State) || !State.Equals(TEXT("Cancelled"), ESearchCase::CaseSensitive)
		|| !Consumption || Consumption->Type != EJson::Null
		|| !Object->TryGetObjectField(TEXT("cancellation"), Cancellation) || !Cancellation
		|| !(*Cancellation)->TryGetStringField(TEXT("dungeonSessionId"), Parsed.DungeonSessionId)
		|| !FGuid::ParseExact(Parsed.DungeonSessionId, EGuidFormats::DigitsWithHyphensLower, SessionGuid) || !SessionGuid.IsValid()
		|| !Parsed.DungeonSessionId.Equals(SessionGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
		|| !(*Cancellation)->TryGetStringField(TEXT("serverId"), Parsed.ServerId) || Parsed.ServerId.IsEmpty() || Parsed.ServerId.Len() > 128
		|| !(*Cancellation)->TryGetStringField(TEXT("consumptionRequestId"), Parsed.ConsumptionRequestId)
		|| !FGuid::ParseExact(Parsed.ConsumptionRequestId, EGuidFormats::DigitsWithHyphensLower, RequestGuid) || !RequestGuid.IsValid()
		|| !Parsed.ConsumptionRequestId.Equals(RequestGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
		|| !(*Cancellation)->TryGetStringField(TEXT("ticketHash"), Parsed.TicketHash) || Parsed.TicketHash.Len() != 64
		|| !(*Cancellation)->TryGetStringField(TEXT("expectedSteamId"), Parsed.ExpectedSteamId)
		|| Parsed.ExpectedSteamId.IsEmpty() || Parsed.ExpectedSteamId.Len() > 20
		|| !(*Cancellation)->TryGetStringField(TEXT("cancelledAt"), CancelledAt)
		|| (!CancelledAt.EndsWith(TEXT("Z"), ESearchCase::CaseSensitive) && !CancelledAt.EndsWith(TEXT("+00:00"), ESearchCase::CaseSensitive))
		|| !FDateTime::ParseIso8601(*CancelledAt, Parsed.CancelledAtUtc) || Parsed.CancelledAtUtc.GetTicks() <= 0) return false;
	for (const TCHAR C : Parsed.ServerId)
		if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'))
			|| (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('-') || C == TEXT('_') || C == TEXT('.'))) return false;
	for (const TCHAR C : Parsed.TicketHash)
		if (!((C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('A') && C <= TEXT('F')))) return false;
	for (const TCHAR C : Parsed.ExpectedSteamId)
		if (C < TEXT('0') || C > TEXT('9')) return false;
	OutCancellation = MoveTemp(Parsed);
	return true;
}

bool FRPGDungeonAdmissionConfirmation::Decode(const FString& Json, int32 HttpStatus)
{
	if (HttpStatus != 200 || Json.IsEmpty() || Json.Len() > 4096) return false;
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	bool bConfirmed = false;
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object) return false;
	const TSharedPtr<FJsonValue> Confirmation = Object->TryGetField(TEXT("admissionConfirmed"));
	return Confirmation && Confirmation->Type == EJson::Boolean
		&& Confirmation->TryGetBool(bConfirmed) && bConfirmed;
}

bool FRPGDungeonConnectionRequest::Encode(const FString& DungeonSessionId,
	const FString& CharacterId, const FString& SteamId,
	int64 ConnectionGeneration, FString& OutBody)
{
	OutBody.Reset();
	FGuid SessionGuid;
	FGuid CharacterGuid;
	if (!FGuid::Parse(DungeonSessionId, SessionGuid) || !SessionGuid.IsValid()
		|| !FGuid::Parse(CharacterId, CharacterGuid) || !CharacterGuid.IsValid()
		|| !DungeonSessionId.Equals(SessionGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
		|| !CharacterId.Equals(CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower), ESearchCase::CaseSensitive)
		|| SteamId.IsEmpty() || SteamId.Len() > 20 || ConnectionGeneration <= 0)
		return false;
	for (const TCHAR Character : SteamId)
		if (Character < TEXT('0') || Character > TEXT('9')) return false;

	// SetNumberField routes through double and would round large int64 values.
	OutBody = FString::Printf(TEXT("{\"steamId\":\"%s\",\"connectionGeneration\":%s}"),
		*SteamId, *LexToString(ConnectionGeneration));
	return true;
}

bool FRPGDungeonConnectionRequest::EncodeDisconnection(const FString& DungeonSessionId,
	const FString& CharacterId, const FString& SteamId, int64 ConnectionGeneration,
	FDateTime ObservedAtUtc, FString& OutBody)
{
	OutBody.Reset();
	FString Identity;
	if (ObservedAtUtc.GetTicks() <= 0 || !Encode(DungeonSessionId, CharacterId,
		SteamId, ConnectionGeneration, Identity)) return false;
	OutBody = Identity.LeftChop(1) + FString::Printf(TEXT(",\"observedAtUtc\":\"%s\"}"),
		*ObservedAtUtc.ToIso8601());
	return true;
}

float FRPGDungeonDisconnectionRetryPolicy::DelaySeconds(
	int32 CompletedAttempt, int32 HttpStatus)
{
	if (CompletedAttempt < 1 || CompletedAttempt >= 3 || HttpStatus == 200
		|| (HttpStatus >= 400 && HttpStatus < 500)) return -1.f;
	return CompletedAttempt == 1 ? 1.f : 3.f;
}
