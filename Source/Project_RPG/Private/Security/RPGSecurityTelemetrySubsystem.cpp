#include "Security/RPGSecurityTelemetrySubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/RPGPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSecurityTelemetrySubsystem)

DEFINE_LOG_CATEGORY_STATIC(LogRPGSecurityTelemetry, Log, All);

namespace RPGSecurityTelemetry
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

	FString SanitizeDetail(FString Detail)
	{
		for (TCHAR& Character : Detail)
		{
			if (FChar::IsControl(Character))
			{
				Character = TEXT(' ');
			}
		}
		return Detail.Left(512);
	}

	const ARPGPlayerState* ResolvePlayerState(const AActor* ObservedActor)
	{
		if (const ARPGPlayerState* DirectState =
			Cast<ARPGPlayerState>(ObservedActor))
		{
			return DirectState;
		}
		if (const APawn* Pawn = Cast<APawn>(ObservedActor))
		{
			return Pawn->GetPlayerState<ARPGPlayerState>();
		}
		if (const AController* Controller = Cast<AController>(ObservedActor))
		{
			return Controller->GetPlayerState<ARPGPlayerState>();
		}
		return nullptr;
	}
}

void URPGSecurityTelemetrySubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bShuttingDown = false;
	Queue.SetCapacity(QueueCapacity);
	if (!bEnabled || !IsRunningDedicatedServer())
	{
		return;
	}

	GameServerToken = FPlatformMisc::GetEnvironmentVariable(
		TEXT("PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN")).TrimStartAndEnd();
	DungeonSessionId = FPlatformMisc::GetEnvironmentVariable(
		TEXT("PROJECT_RPG_DUNGEON_SESSION_ID")).TrimStartAndEnd();
	FGuid ParsedDungeonSessionId;
	if (GameServerToken.IsEmpty()
		|| !FGuid::Parse(DungeonSessionId, ParsedDungeonSessionId))
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry is unavailable because the dedicated "
				"server token or dungeon session ID is not configured."));
		return;
	}

	ApiUrl = ApiUrl.TrimStartAndEnd();
	ApiUrl.RemoveFromEnd(TEXT("/"));
	if (ApiUrl.IsEmpty())
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry is unavailable because ApiUrl is empty."));
		return;
	}

	DungeonSessionId = ParsedDungeonSessionId.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	BatchSize = FMath::Clamp(BatchSize, 1, 64);
	MaximumAttempts = FMath::Clamp(MaximumAttempts, 1, 10);
	const FString SpoolDirectory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("SecurityTelemetry"));
	SpoolPath = FPaths::Combine(
		SpoolDirectory,
		DungeonSessionId + TEXT(".jsonl"));
	DeadLetterPath = FPaths::Combine(
		SpoolDirectory,
		DungeonSessionId + TEXT(".deadletter.jsonl"));
	bAvailable = true;
	LoadPersistedEvents();
	PostWorldInitializationHandle =
		FWorldDelegates::OnPostWorldInitialization.AddUObject(
			this,
			&ThisClass::HandlePostWorldInitialization);
	ScheduleFlush(0.0f);
}

void URPGSecurityTelemetrySubsystem::Deinitialize()
{
	bShuttingDown = true;
	bAvailable = false;
	if (PostWorldInitializationHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldInitialization.Remove(
			PostWorldInitializationHandle);
		PostWorldInitializationHandle.Reset();
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWorld* World = GameInstance->GetWorld())
		{
			World->GetTimerManager().ClearTimer(FlushTimerHandle);
		}
	}
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->OnProcessRequestComplete().Unbind();
		ActiveRequest->CancelRequest();
		ActiveRequest.Reset();
	}
	if (Queue.Num() > 0)
	{
		PersistQueueSnapshot();
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry shut down with %d unacknowledged events."),
			Queue.Num());
	}
	Queue.Reset();
	GameServerToken.Reset();
	DungeonSessionId.Reset();
	SpoolPath.Reset();
	DeadLetterPath.Reset();
	Super::Deinitialize();
}

bool URPGSecurityTelemetrySubsystem::EnqueueViolation(
	const AActor* ObservedActor,
	const FRPGSecurityViolation& Violation,
	const float RiskAfter)
{
	if (!bAvailable || bShuttingDown || !IsValid(ObservedActor)
		|| !ObservedActor->HasAuthority())
	{
		return false;
	}

	const ARPGPlayerState* PlayerState =
		RPGSecurityTelemetry::ResolvePlayerState(ObservedActor);
	if (!PlayerState || !PlayerState->HasAuthenticatedCharacter()
		|| !PlayerState->GetBackendDungeonSessionId().Equals(
			DungeonSessionId,
			ESearchCase::IgnoreCase))
	{
		++DeliveryFailureDropCount;
		return false;
	}

	FGuid CharacterId;
	const FString SteamId =
		PlayerState->GetAuthenticatedSteamId().TrimStartAndEnd();
	if (!FGuid::Parse(PlayerState->GetBackendCharacterId(), CharacterId)
		|| !RPGSecurityTelemetry::IsAsciiDigits(SteamId)
		|| !FMath::IsFinite(Violation.Score)
		|| !FMath::IsFinite(RiskAfter)
		|| !FMath::IsFinite(Violation.ServerTimeSeconds))
	{
		++DeliveryFailureDropCount;
		return false;
	}

	FRPGSecurityTelemetryEvent Event;
	Event.EventId = FGuid::NewGuid();
	Event.CharacterId = CharacterId.ToString(
		EGuidFormats::DigitsWithHyphensLower);
	Event.SteamId = SteamId;
	Event.Type = Violation.Type;
	Event.Severity = Violation.Severity;
	Event.Score = FMath::Clamp(
		static_cast<double>(Violation.Score),
		0.0,
		100000.0);
	Event.RiskAfter = FMath::Clamp(
		static_cast<double>(RiskAfter),
		0.0,
		100000.0);
	Event.ServerTimeSeconds = FMath::Max(
		0.0,
		Violation.ServerTimeSeconds);
	Event.Detail = RPGSecurityTelemetry::SanitizeDetail(Violation.Detail);
	const FRPGSecurityTelemetryEvent PersistedEvent = Event;
	const int64 DroppedCountBeforeEnqueue = Queue.GetDroppedCount();
	if (!Queue.Enqueue(MoveTemp(Event)))
	{
		return false;
	}
	const bool bQueueEvictedEvent =
		Queue.GetDroppedCount() != DroppedCountBeforeEnqueue;
	const bool bPersisted = bQueueEvictedEvent
		? PersistQueueSnapshot()
		: AppendEventToFile(PersistedEvent, SpoolPath);
	if (bPersistToDisk && !bPersisted)
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry could not persist event %s to disk."),
			*PersistedEvent.EventId.ToString());
	}

	if (Queue.Num() >= BatchSize)
	{
		ScheduleFlush(0.0f);
	}
	else
	{
		ScheduleFlush(FlushIntervalSeconds);
	}
	return true;
}

bool URPGSecurityTelemetrySubsystem::FlushNow()
{
	if (!bAvailable || bShuttingDown || Queue.Num() == 0
		|| Queue.HasBatchInFlight() || ActiveRequest.IsValid())
	{
		return false;
	}
	Flush();
	return ActiveRequest.IsValid();
}

void URPGSecurityTelemetrySubsystem::ScheduleFlush(const float DelaySeconds)
{
	if (!bAvailable || bShuttingDown || Queue.Num() == 0
		|| Queue.HasBatchInFlight() || ActiveRequest.IsValid())
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		FlushTimerHandle,
		this,
		&URPGSecurityTelemetrySubsystem::Flush,
		FMath::Max(0.01f, DelaySeconds),
		false);
}

void URPGSecurityTelemetrySubsystem::Flush()
{
	if (!bAvailable || bShuttingDown || ActiveRequest.IsValid())
	{
		return;
	}

	TArray<FRPGSecurityTelemetryEvent> Batch =
		Queue.BeginBatch(BatchSize);
	if (Batch.IsEmpty())
	{
		return;
	}

	FString Body;
	if (!FRPGSecurityTelemetryJsonCodec::SerializeBatch(
		DungeonSessionId,
		Batch,
		Body))
	{
		AppendReservedBatchToDeadLetter();
		DeliveryFailureDropCount += Queue.GetReservedCount();
		Queue.CompleteBatch(true);
		PersistQueueSnapshot();
		CurrentBatchAttempt = 0;
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Discarded a security telemetry batch that could not be encoded."));
		ScheduleFlush(0.0f);
		return;
	}

	CurrentBatchAttempt = FMath::Min(
		CurrentBatchAttempt + 1,
		MaximumAttempts);
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiUrl / TEXT("security/events/batch"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(
		TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *GameServerToken));
	Request->SetContentAsString(Body);
	Request->SetTimeout(FMath::Max(1.0f, RequestTimeoutSeconds));
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&URPGSecurityTelemetrySubsystem::HandleRequestComplete);
	ActiveRequest = Request;
	if (!Request->ProcessRequest())
	{
		Request->OnProcessRequestComplete().Unbind();
		ActiveRequest.Reset();
		Queue.CompleteBatch(false);
		ScheduleFlush(GetRetryDelaySeconds());
	}
}

void URPGSecurityTelemetrySubsystem::HandleRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bWasSuccessful)
{
	if (Request != ActiveRequest)
	{
		return;
	}
	ActiveRequest.Reset();
	if (bShuttingDown)
	{
		return;
	}

	const int32 StatusCode = Response.IsValid()
		? Response->GetResponseCode()
		: 0;
	const bool bAcknowledged = bWasSuccessful && Response.IsValid()
		&& StatusCode >= 200 && StatusCode < 300
		&& FRPGSecurityTelemetryJsonCodec::IsBatchAcknowledgementValid(
			Response->GetContentAsString(),
			Queue.GetReservedCount());
	const bool bPermanentFailure = Response.IsValid()
		&& StatusCode >= 400 && StatusCode < 500
		&& StatusCode != 408 && StatusCode != 429;
	const bool bDiscardBatch = bAcknowledged || bPermanentFailure;
	if (bDiscardBatch && !bAcknowledged)
	{
		AppendReservedBatchToDeadLetter();
		DeliveryFailureDropCount += Queue.GetReservedCount();
		const FString ResponseDetail = Response.IsValid()
			? RPGSecurityTelemetry::SanitizeDetail(
				Response->GetContentAsString())
			: TEXT("No HTTP response");
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Discarded security telemetry after HTTP %d and %d attempt(s): %s"),
			StatusCode,
			CurrentBatchAttempt,
			*ResponseDetail);
	}

	Queue.CompleteBatch(bDiscardBatch);
	if (bDiscardBatch)
	{
		CurrentBatchAttempt = 0;
		PersistQueueSnapshot();
	}
	ScheduleFlush(
		bDiscardBatch ? 0.0f : GetRetryDelaySeconds());
}

void URPGSecurityTelemetrySubsystem::HandlePostWorldInitialization(
	UWorld* World,
	const UWorld::InitializationValues InitializationValues)
{
	if (!bAvailable || !World || !World->IsGameWorld()
		|| World->GetGameInstance() != GetGameInstance())
	{
		return;
	}
	ScheduleFlush(0.0f);
}

void URPGSecurityTelemetrySubsystem::LoadPersistedEvents()
{
	if (!bPersistToDisk || SpoolPath.IsEmpty()
		|| !IFileManager::Get().FileExists(*SpoolPath))
	{
		return;
	}

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *SpoolPath))
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry could not read spool %s."),
			*SpoolPath);
		return;
	}

	int32 RecoveredCount = 0;
	int32 InvalidLineCount = 0;
	for (const FString& Line : Lines)
	{
		if (Line.IsEmpty())
		{
			continue;
		}
		FString PersistedSessionId;
		TArray<FRPGSecurityTelemetryEvent> PersistedEvents;
		if (!FRPGSecurityTelemetryJsonCodec::DeserializeBatch(
			Line,
			PersistedSessionId,
			PersistedEvents)
			|| !PersistedSessionId.Equals(
				DungeonSessionId,
				ESearchCase::IgnoreCase))
		{
			++InvalidLineCount;
			continue;
		}
		for (FRPGSecurityTelemetryEvent& Event : PersistedEvents)
		{
			RecoveredCount += Queue.Enqueue(MoveTemp(Event)) ? 1 : 0;
		}
	}
	PersistQueueSnapshot();
	if (InvalidLineCount > 0)
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry recovered %d event(s); invalid spool lines=%d."),
			RecoveredCount,
			InvalidLineCount);
	}
	else
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Log,
			TEXT("Security telemetry recovered %d event(s)."),
			RecoveredCount);
	}
}

bool URPGSecurityTelemetrySubsystem::AppendEventToFile(
	const FRPGSecurityTelemetryEvent& Event,
	const FString& FilePath) const
{
	if (!bPersistToDisk)
	{
		return true;
	}
	if (FilePath.IsEmpty()
		|| !IFileManager::Get().MakeDirectory(
			*FPaths::GetPath(FilePath),
			true))
	{
		return false;
	}

	const TArray<FRPGSecurityTelemetryEvent> Events = { Event };
	FString Json;
	if (!FRPGSecurityTelemetryJsonCodec::SerializeBatch(
		DungeonSessionId,
		Events,
		Json))
	{
		return false;
	}
	Json.Append(LINE_TERMINATOR);
	return FFileHelper::SaveStringToFile(
		Json,
		*FilePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
		&IFileManager::Get(),
		EFileWrite::FILEWRITE_Append);
}

bool URPGSecurityTelemetrySubsystem::AppendReservedBatchToDeadLetter() const
{
	bool bSucceeded = true;
	for (const FRPGSecurityTelemetryEvent& Event : Queue.GetReservedEvents())
	{
		bSucceeded = AppendEventToFile(Event, DeadLetterPath) && bSucceeded;
	}
	return bSucceeded;
}

bool URPGSecurityTelemetrySubsystem::PersistQueueSnapshot() const
{
	if (!bPersistToDisk)
	{
		return true;
	}
	if (SpoolPath.IsEmpty())
	{
		return false;
	}
	if (Queue.Num() == 0)
	{
		return !IFileManager::Get().FileExists(*SpoolPath)
			|| IFileManager::Get().Delete(*SpoolPath, false, true, true);
	}
	if (!IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(SpoolPath),
		true))
	{
		return false;
	}

	FString Contents;
	for (const FRPGSecurityTelemetryEvent& Event : Queue.GetEvents())
	{
		const TArray<FRPGSecurityTelemetryEvent> Events = { Event };
		FString Json;
		if (!FRPGSecurityTelemetryJsonCodec::SerializeBatch(
			DungeonSessionId,
			Events,
			Json))
		{
			return false;
		}
		Contents.Append(Json);
		Contents.Append(LINE_TERMINATOR);
	}

	const FString TemporaryPath = SpoolPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(
		Contents,
		*TemporaryPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return false;
	}
	return IFileManager::Get().Move(
		*SpoolPath,
		*TemporaryPath,
		true,
		true,
		false,
		true);
}

float URPGSecurityTelemetrySubsystem::GetRetryDelaySeconds() const
{
	const int32 Exponent = FMath::Clamp(CurrentBatchAttempt - 1, 0, 8);
	return FMath::Min(
		FMath::Max(0.1f, FlushIntervalSeconds)
			* static_cast<float>(1 << Exponent),
		FMath::Max(1.0f, MaximumRetryDelaySeconds));
}
