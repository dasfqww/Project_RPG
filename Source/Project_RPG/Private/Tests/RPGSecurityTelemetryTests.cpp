#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Security/RPGSecurityTelemetrySubsystem.h"
#include "Security/RPGSecurityTelemetryTypes.h"
#include "Serialization/JsonSerializer.h"

namespace RPGSecurityTelemetryTests
{
	FRPGSecurityTelemetryEvent MakeEvent(
		const FGuid& EventId,
		const FString& Detail = TEXT("test"))
	{
		FRPGSecurityTelemetryEvent Event;
		Event.EventId = EventId;
		Event.CharacterId = TEXT("11111111-2222-3333-4444-555555555555");
		Event.SteamId = TEXT("76561198000000000");
		Event.Type = ERPGSecurityViolationType::InvalidCombatHit;
		Event.Severity = ERPGSecurityViolationSeverity::High;
		Event.Score = 5.0;
		Event.RiskAfter = 17.0;
		Event.ServerTimeSeconds = 42.5;
		Event.Detail = Detail;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTelemetryQueueRetryTest,
	"ProjectRPG.Security.Telemetry.QueuePreservesReservedBatchForRetry",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTelemetryQueueRetryTest::RunTest(const FString& Parameters)
{
	const FGuid First = FGuid::NewGuid();
	const FGuid Second = FGuid::NewGuid();
	const FGuid Third = FGuid::NewGuid();
	const FGuid Fourth = FGuid::NewGuid();
	FRPGSecurityTelemetryQueue Queue(3);
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(First));
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Second));
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Third));

	const TArray<FRPGSecurityTelemetryEvent> FirstBatch = Queue.BeginBatch(2);
	TestEqual(TEXT("Two events are reserved"), FirstBatch.Num(), 2);
	TestEqual(TEXT("First reserved event is FIFO"), FirstBatch[0].EventId, First);
	TestEqual(TEXT("Second reserved event is FIFO"), FirstBatch[1].EventId, Second);
	TestTrue(
		TEXT("A new event can evict the oldest non-reserved event"),
		Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Fourth)));
	TestEqual(TEXT("Queue remains bounded"), Queue.Num(), 3);
	TestEqual(TEXT("One non-reserved event was dropped"), Queue.GetDroppedCount(), 1LL);

	Queue.CompleteBatch(false);
	const TArray<FRPGSecurityTelemetryEvent> RetryBatch = Queue.BeginBatch(3);
	TestEqual(TEXT("Failed batch remains queued"), RetryBatch.Num(), 3);
	TestEqual(TEXT("First event survives retry"), RetryBatch[0].EventId, First);
	TestEqual(TEXT("Second event survives retry"), RetryBatch[1].EventId, Second);
	TestEqual(TEXT("Newest event remains queued"), RetryBatch[2].EventId, Fourth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTelemetryQueueAckTest,
	"ProjectRPG.Security.Telemetry.QueueRemovesOnlyAcknowledgedBatch",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTelemetryQueueAckTest::RunTest(const FString& Parameters)
{
	const FGuid First = FGuid::NewGuid();
	const FGuid Second = FGuid::NewGuid();
	const FGuid Third = FGuid::NewGuid();
	const FGuid Fourth = FGuid::NewGuid();
	FRPGSecurityTelemetryQueue Queue(4);
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(First));
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Second));
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Third));
	Queue.BeginBatch(2);
	Queue.Enqueue(RPGSecurityTelemetryTests::MakeEvent(Fourth));

	Queue.CompleteBatch(true);
	const TArray<FRPGSecurityTelemetryEvent> Remaining = Queue.BeginBatch(4);
	TestEqual(TEXT("Only unacknowledged events remain"), Remaining.Num(), 2);
	TestEqual(TEXT("Pre-existing tail remains"), Remaining[0].EventId, Third);
	TestEqual(TEXT("Event added during flight remains"), Remaining[1].EventId, Fourth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTelemetryQueueSaturationTest,
	"ProjectRPG.Security.Telemetry.QueueSaturationProtectsCriticalEvidence",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTelemetryQueueSaturationTest::RunTest(const FString& Parameters)
{
	auto MakeSeverityEvent = [](const ERPGSecurityViolationSeverity Severity)
	{
		FRPGSecurityTelemetryEvent Event =
			RPGSecurityTelemetryTests::MakeEvent(FGuid::NewGuid());
		Event.Severity = Severity;
		return Event;
	};

	FRPGSecurityTelemetryQueue Queue(4);
	const FRPGSecurityTelemetryEvent ReservedFirst =
		MakeSeverityEvent(ERPGSecurityViolationSeverity::Low);
	const FRPGSecurityTelemetryEvent ReservedSecond =
		MakeSeverityEvent(ERPGSecurityViolationSeverity::Medium);
	const FRPGSecurityTelemetryEvent PendingCritical =
		MakeSeverityEvent(ERPGSecurityViolationSeverity::Critical);
	Queue.Enqueue(ReservedFirst);
	Queue.Enqueue(ReservedSecond);
	Queue.Enqueue(MakeSeverityEvent(ERPGSecurityViolationSeverity::Low));
	Queue.Enqueue(PendingCritical);
	Queue.BeginBatch(2);

	// Sustained low-priority observations may replace one another, but must
	// never evict the reserved retry batch or the unreserved critical event.
	for (int32 Index = 0; Index < 1024; ++Index)
	{
		TestTrue(TEXT("Low-priority tail remains replaceable"),
			Queue.Enqueue(MakeSeverityEvent(ERPGSecurityViolationSeverity::Low)));
		TestEqual(TEXT("Queue stays bounded throughout the outage"),
			Queue.Num(), 4);
	}
	TestEqual(TEXT("Each replacement is counted"),
		Queue.GetDroppedCount(), 1024LL);
	TestEqual(TEXT("Critical evidence survives low-priority flood"),
		Queue.GetEvents()[2].EventId, PendingCritical.EventId);

	const FRPGSecurityTelemetryEvent NewHigh =
		MakeSeverityEvent(ERPGSecurityViolationSeverity::High);
	TestTrue(TEXT("High priority evicts the low-priority tail"),
		Queue.Enqueue(NewHigh));
	TestFalse(TEXT("Low priority cannot evict a high-priority tail"),
		Queue.Enqueue(MakeSeverityEvent(ERPGSecurityViolationSeverity::Low)));
	TestEqual(TEXT("Rejected arrival is counted"),
		Queue.GetDroppedCount(), 1026LL);

	Queue.CompleteBatch(false);
	const TArray<FRPGSecurityTelemetryEvent> Retry = Queue.BeginBatch(4);
	TestEqual(TEXT("All surviving events are retried"), Retry.Num(), 4);
	if (Retry.Num() == 4)
	{
		TestEqual(TEXT("Reserved first event survives"),
			Retry[0].EventId, ReservedFirst.EventId);
		TestEqual(TEXT("Reserved second event survives"),
			Retry[1].EventId, ReservedSecond.EventId);
		TestEqual(TEXT("Critical event survives"),
			Retry[2].EventId, PendingCritical.EventId);
		TestEqual(TEXT("High event survives"),
			Retry[3].EventId, NewHigh.EventId);
	}
	TestFalse(TEXT("Entire in-flight queue cannot be evicted"),
		Queue.Enqueue(MakeSeverityEvent(ERPGSecurityViolationSeverity::Critical)));
	TestEqual(TEXT("Rejected all-reserved arrival is counted"),
		Queue.GetDroppedCount(), 1027LL);
	Queue.CompleteBatch(true);
	TestEqual(TEXT("Acknowledgement clears surviving evidence"), Queue.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTelemetryScheduleTest,
	"ProjectRPG.Security.Telemetry.NewEventsPreserveFlushAndRetryDeadlines",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTelemetryScheduleTest::RunTest(const FString& Parameters)
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	const FName WorldPackageName(*FString::Printf(
		TEXT("/Temp/SecurityTelemetryTimers_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	GameInstance->InitializeForMinimalNetRPC(WorldPackageName);
	UWorld* World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("Telemetry timer test world"), World))
	{
		GameInstance->Shutdown();
		return false;
	}
	URPGSecurityTelemetrySubsystem* Telemetry =
		NewObject<URPGSecurityTelemetrySubsystem>(GameInstance);
	Telemetry->bAvailable = true;
	Telemetry->FlushIntervalSeconds = 2.0f;
	Telemetry->MaximumRetryDelaySeconds = 30.0f;
	Telemetry->Queue.Enqueue(
		RPGSecurityTelemetryTests::MakeEvent(FGuid::NewGuid()));
	FTimerManager& Timers = World->GetTimerManager();

	Telemetry->ScheduleFlush(2.0f);
	Telemetry->ScheduleFlush(4.0f);
	TestEqual(TEXT("New observations cannot postpone a healthy flush"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 2.0f);
	Telemetry->ScheduleFlush(0.1f);
	TestEqual(TEXT("A healthy full batch can advance the deadline"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 0.1f);
	Telemetry->ScheduleFlush(2.0f);
	TestEqual(TEXT("Later observations cannot postpone an expedited flush"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 0.1f);

	Timers.ClearTimer(Telemetry->FlushTimerHandle);
	Telemetry->CurrentBatchAttempt = 3;
	Telemetry->ScheduleFlush(Telemetry->GetRetryDelaySeconds());
	Telemetry->ScheduleFlush(0.0f);
	TestEqual(TEXT("A full queue cannot bypass the 8-second retry backoff"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 8.0f);
	Telemetry->ScheduleFlush(2.0f);
	TestEqual(TEXT("New observations cannot shorten retry backoff"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 8.0f);
	Telemetry->ScheduleFlush(30.0f);
	TestEqual(TEXT("New observations cannot extend retry backoff"),
		Timers.GetTimerRemaining(Telemetry->FlushTimerHandle), 8.0f);

	Timers.ClearTimer(Telemetry->FlushTimerHandle);
	Telemetry->bAvailable = false;
	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTelemetryJsonTest,
	"ProjectRPG.Security.Telemetry.SerializesBackendBatchContract",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTelemetryJsonTest::RunTest(const FString& Parameters)
{
	const FString SessionId = TEXT("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
	const FGuid EventId = FGuid::NewGuid();
	const TArray<FRPGSecurityTelemetryEvent> Events = {
		RPGSecurityTelemetryTests::MakeEvent(EventId, TEXT("server observed hit"))
	};
	FString Json;
	TestTrue(
		TEXT("Valid telemetry batch serializes"),
		FRPGSecurityTelemetryJsonCodec::SerializeBatch(
			SessionId,
			Events,
			Json));
	TestFalse(
		TEXT("Persisted telemetry is one JSONL record"),
		Json.Contains(TEXT("\n")) || Json.Contains(TEXT("\r")));

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	TestTrue(
		TEXT("Serialized telemetry is valid JSON"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
	if (!Root.IsValid())
	{
		return false;
	}

	TestEqual(
		TEXT("Session field uses backend contract casing"),
		Root->GetStringField(TEXT("dungeonSessionId")),
		SessionId);
	const TArray<TSharedPtr<FJsonValue>>& JsonEvents =
		Root->GetArrayField(TEXT("events"));
	TestEqual(TEXT("One event is encoded"), JsonEvents.Num(), 1);
	const TSharedPtr<FJsonObject> JsonEvent = JsonEvents[0]->AsObject();
	TestTrue(TEXT("Event payload is an object"), JsonEvent.IsValid());
	if (!JsonEvent.IsValid())
	{
		return false;
	}

	TestEqual(
		TEXT("Violation type name matches backend enum"),
		JsonEvent->GetStringField(TEXT("type")),
		FString(TEXT("InvalidCombatHit")));
	TestEqual(
		TEXT("Severity name matches backend enum"),
		JsonEvent->GetStringField(TEXT("severity")),
		FString(TEXT("High")));
	TestEqual(
		TEXT("Risk score is preserved"),
		JsonEvent->GetNumberField(TEXT("riskAfter")),
		17.0);
	TestEqual(
		TEXT("Detail is preserved"),
		JsonEvent->GetStringField(TEXT("detail")),
		FString(TEXT("server observed hit")));
	FString ParsedSessionId;
	TArray<FRPGSecurityTelemetryEvent> ParsedEvents;
	TestTrue(
		TEXT("Persisted telemetry can be deserialized"),
		FRPGSecurityTelemetryJsonCodec::DeserializeBatch(
			Json,
			ParsedSessionId,
			ParsedEvents));
	TestEqual(
		TEXT("Persisted session identity is preserved"),
		ParsedSessionId,
		SessionId);
	TestEqual(
		TEXT("One persisted event is recovered"),
		ParsedEvents.Num(),
		1);
	if (ParsedEvents.Num() == 1)
	{
		TestEqual(
			TEXT("Recovered event identity is stable"),
			ParsedEvents[0].EventId,
			EventId);
		TestEqual(
			TEXT("Recovered event risk is stable"),
			ParsedEvents[0].RiskAfter,
			17.0);
	}
	TestTrue(
		TEXT("Complete backend acknowledgement is accepted"),
		FRPGSecurityTelemetryJsonCodec::IsBatchAcknowledgementValid(
			TEXT("{\"acceptedCount\":1,\"duplicateCount\":0}"),
			1));
	TestFalse(
		TEXT("Partial backend acknowledgement is rejected"),
		FRPGSecurityTelemetryJsonCodec::IsBatchAcknowledgementValid(
			TEXT("{\"acceptedCount\":0,\"duplicateCount\":0}"),
			1));
	return true;
}

#endif
