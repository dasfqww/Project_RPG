#include "Security/RPGSecurityTelemetryTypes.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace RPGSecurityTelemetryCodec
{
	bool IsAsciiDigits(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 20)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return false;
			}
		}
		return true;
	}

	bool HasControlCharacter(const FString& Value)
	{
		for (const TCHAR Character : Value)
		{
			if (FChar::IsControl(Character))
			{
				return true;
			}
		}
		return false;
	}
}

FRPGSecurityTelemetryQueue::FRPGSecurityTelemetryQueue(const int32 InCapacity)
{
	SetCapacity(InCapacity);
}

void FRPGSecurityTelemetryQueue::SetCapacity(const int32 InCapacity)
{
	Capacity = FMath::Max(1, InCapacity);
	while (Events.Num() > Capacity)
	{
		const int32 EvictionIndex = FindOldestEvictableIndex();
		if (EvictionIndex == INDEX_NONE)
		{
			break;
		}
		Events.RemoveAt(EvictionIndex, EAllowShrinking::No);
		++DroppedCount;
	}
}

bool FRPGSecurityTelemetryQueue::Enqueue(FRPGSecurityTelemetryEvent Event)
{
	if (!Event.EventId.IsValid() || Events.ContainsByPredicate(
		[&Event](const FRPGSecurityTelemetryEvent& Existing)
		{
			return Existing.EventId == Event.EventId;
		}))
	{
		++DroppedCount;
		return false;
	}

	if (Events.Num() >= Capacity)
	{
		const int32 EvictionIndex = FindOldestEvictableIndex();
		if (EvictionIndex == INDEX_NONE)
		{
			++DroppedCount;
			return false;
		}
		Events.RemoveAt(EvictionIndex, EAllowShrinking::No);
		++DroppedCount;
	}

	Events.Add(MoveTemp(Event));
	return true;
}

TArray<FRPGSecurityTelemetryEvent> FRPGSecurityTelemetryQueue::BeginBatch(
	const int32 MaximumBatchSize)
{
	if (HasBatchInFlight() || MaximumBatchSize <= 0 || Events.IsEmpty())
	{
		return {};
	}

	const int32 BatchSize = FMath::Min(MaximumBatchSize, Events.Num());
	TArray<FRPGSecurityTelemetryEvent> Batch;
	Batch.Reserve(BatchSize);
	ReservedEventIds.Reserve(BatchSize);
	for (int32 Index = 0; Index < BatchSize; ++Index)
	{
		Batch.Add(Events[Index]);
		ReservedEventIds.Add(Events[Index].EventId);
	}
	return Batch;
}

void FRPGSecurityTelemetryQueue::CompleteBatch(
	const bool bRemoveReservedEvents)
{
	if (bRemoveReservedEvents && !ReservedEventIds.IsEmpty())
	{
		Events.RemoveAll(
			[this](const FRPGSecurityTelemetryEvent& Event)
			{
				return IsReserved(Event.EventId);
			});
	}
	ReservedEventIds.Reset();
}

void FRPGSecurityTelemetryQueue::Reset()
{
	Events.Reset();
	ReservedEventIds.Reset();
	DroppedCount = 0;
}

TArray<FRPGSecurityTelemetryEvent>
FRPGSecurityTelemetryQueue::GetReservedEvents() const
{
	TArray<FRPGSecurityTelemetryEvent> Result;
	Result.Reserve(ReservedEventIds.Num());
	for (const FRPGSecurityTelemetryEvent& Event : Events)
	{
		if (IsReserved(Event.EventId))
		{
			Result.Add(Event);
		}
	}
	return Result;
}

bool FRPGSecurityTelemetryQueue::IsReserved(const FGuid& EventId) const
{
	return ReservedEventIds.Contains(EventId);
}

int32 FRPGSecurityTelemetryQueue::FindOldestEvictableIndex() const
{
	for (int32 Index = 0; Index < Events.Num(); ++Index)
	{
		if (!IsReserved(Events[Index].EventId))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FRPGSecurityTelemetryJsonCodec::SerializeBatch(
	const FString& DungeonSessionId,
	const TConstArrayView<FRPGSecurityTelemetryEvent> Events,
	FString& OutJson)
{
	OutJson.Reset();
	FGuid ParsedSessionId;
	if (!FGuid::Parse(DungeonSessionId, ParsedSessionId) || Events.IsEmpty())
	{
		return false;
	}

	const UEnum* TypeEnum = StaticEnum<ERPGSecurityViolationType>();
	const UEnum* SeverityEnum = StaticEnum<ERPGSecurityViolationSeverity>();
	if (!TypeEnum || !SeverityEnum)
	{
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("dungeonSessionId"),
		ParsedSessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
	TArray<TSharedPtr<FJsonValue>> JsonEvents;
	JsonEvents.Reserve(Events.Num());
	for (const FRPGSecurityTelemetryEvent& Event : Events)
	{
		FGuid CharacterId;
		if (!Event.EventId.IsValid()
			|| !FGuid::Parse(Event.CharacterId, CharacterId)
			|| !FMath::IsFinite(Event.Score)
			|| !FMath::IsFinite(Event.RiskAfter)
			|| !FMath::IsFinite(Event.ServerTimeSeconds))
		{
			return false;
		}

		TSharedRef<FJsonObject> JsonEvent = MakeShared<FJsonObject>();
		JsonEvent->SetStringField(
			TEXT("eventId"),
			Event.EventId.ToString(EGuidFormats::DigitsWithHyphensLower));
		JsonEvent->SetStringField(
			TEXT("characterId"),
			CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower));
		JsonEvent->SetStringField(TEXT("steamId"), Event.SteamId);
		JsonEvent->SetStringField(
			TEXT("type"),
			TypeEnum->GetNameStringByValue(static_cast<int64>(Event.Type)));
		JsonEvent->SetStringField(
			TEXT("severity"),
			SeverityEnum->GetNameStringByValue(
				static_cast<int64>(Event.Severity)));
		JsonEvent->SetNumberField(TEXT("score"), Event.Score);
		JsonEvent->SetNumberField(TEXT("riskAfter"), Event.RiskAfter);
		JsonEvent->SetNumberField(
			TEXT("serverTimeSeconds"),
			Event.ServerTimeSeconds);
		JsonEvent->SetStringField(TEXT("detail"), Event.Detail.Left(512));
		JsonEvents.Add(MakeShared<FJsonValueObject>(JsonEvent));
	}
	Root->SetArrayField(TEXT("events"), JsonEvents);

	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root, Writer);
}

bool FRPGSecurityTelemetryJsonCodec::DeserializeBatch(
	const FString& Json,
	FString& OutDungeonSessionId,
	TArray<FRPGSecurityTelemetryEvent>& OutEvents)
{
	OutDungeonSessionId.Reset();
	OutEvents.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	FString SessionIdText;
	FGuid SessionId;
	const TArray<TSharedPtr<FJsonValue>>* JsonEvents = nullptr;
	if (!Root->TryGetStringField(TEXT("dungeonSessionId"), SessionIdText)
		|| !FGuid::Parse(SessionIdText, SessionId)
		|| !Root->TryGetArrayField(TEXT("events"), JsonEvents)
		|| !JsonEvents || JsonEvents->IsEmpty())
	{
		return false;
	}

	const UEnum* TypeEnum = StaticEnum<ERPGSecurityViolationType>();
	const UEnum* SeverityEnum = StaticEnum<ERPGSecurityViolationSeverity>();
	if (!TypeEnum || !SeverityEnum)
	{
		return false;
	}

	TArray<FRPGSecurityTelemetryEvent> ParsedEvents;
	ParsedEvents.Reserve(JsonEvents->Num());
	for (const TSharedPtr<FJsonValue>& JsonValue : *JsonEvents)
	{
		const TSharedPtr<FJsonObject> JsonEvent = JsonValue.IsValid()
			? JsonValue->AsObject()
			: nullptr;
		FString EventIdText;
		FString CharacterIdText;
		FString SteamId;
		FString TypeText;
		FString SeverityText;
		FString Detail;
		double Score = 0.0;
		double RiskAfter = 0.0;
		double ServerTimeSeconds = 0.0;
		FGuid EventId;
		FGuid CharacterId;
		if (!JsonEvent.IsValid()
			|| !JsonEvent->TryGetStringField(TEXT("eventId"), EventIdText)
			|| !JsonEvent->TryGetStringField(
				TEXT("characterId"),
				CharacterIdText)
			|| !JsonEvent->TryGetStringField(TEXT("steamId"), SteamId)
			|| !JsonEvent->TryGetStringField(TEXT("type"), TypeText)
			|| !JsonEvent->TryGetStringField(TEXT("severity"), SeverityText)
			|| !JsonEvent->TryGetStringField(TEXT("detail"), Detail)
			|| !JsonEvent->TryGetNumberField(TEXT("score"), Score)
			|| !JsonEvent->TryGetNumberField(TEXT("riskAfter"), RiskAfter)
			|| !JsonEvent->TryGetNumberField(
				TEXT("serverTimeSeconds"),
				ServerTimeSeconds)
			|| !FGuid::Parse(EventIdText, EventId)
			|| !EventId.IsValid()
			|| !FGuid::Parse(CharacterIdText, CharacterId)
			|| !CharacterId.IsValid()
			|| !RPGSecurityTelemetryCodec::IsAsciiDigits(SteamId)
			|| !FMath::IsFinite(Score) || Score < 0.0 || Score > 100000.0
			|| !FMath::IsFinite(RiskAfter) || RiskAfter < 0.0
			|| RiskAfter > 100000.0
			|| !FMath::IsFinite(ServerTimeSeconds)
			|| ServerTimeSeconds < 0.0
			|| Detail.Len() > 512
			|| RPGSecurityTelemetryCodec::HasControlCharacter(Detail))
		{
			return false;
		}

		const int64 TypeValue = TypeEnum->GetValueByNameString(TypeText);
		const int64 SeverityValue =
			SeverityEnum->GetValueByNameString(SeverityText);
		if (TypeValue == INDEX_NONE || SeverityValue == INDEX_NONE)
		{
			return false;
		}

		FRPGSecurityTelemetryEvent Event;
		Event.EventId = EventId;
		Event.CharacterId = CharacterId.ToString(
			EGuidFormats::DigitsWithHyphensLower);
		Event.SteamId = SteamId;
		Event.Type = static_cast<ERPGSecurityViolationType>(TypeValue);
		Event.Severity =
			static_cast<ERPGSecurityViolationSeverity>(SeverityValue);
		Event.Score = Score;
		Event.RiskAfter = RiskAfter;
		Event.ServerTimeSeconds = ServerTimeSeconds;
		Event.Detail = Detail;
		ParsedEvents.Add(MoveTemp(Event));
	}

	OutDungeonSessionId = SessionId.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	OutEvents = MoveTemp(ParsedEvents);
	return true;
}

bool FRPGSecurityTelemetryJsonCodec::IsBatchAcknowledgementValid(
	const FString& Json,
	const int32 ExpectedEventCount)
{
	if (ExpectedEventCount <= 0)
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}

	double AcceptedValue = 0.0;
	double DuplicateValue = 0.0;
	if (!Root->TryGetNumberField(TEXT("acceptedCount"), AcceptedValue)
		|| !Root->TryGetNumberField(TEXT("duplicateCount"), DuplicateValue)
		|| !FMath::IsFinite(AcceptedValue)
		|| !FMath::IsFinite(DuplicateValue)
		|| AcceptedValue < 0.0
		|| DuplicateValue < 0.0
		|| AcceptedValue != FMath::TruncToDouble(AcceptedValue)
		|| DuplicateValue != FMath::TruncToDouble(DuplicateValue))
	{
		return false;
	}

	return AcceptedValue + DuplicateValue ==
		static_cast<double>(ExpectedEventCount);
}
