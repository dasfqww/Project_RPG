#include "Security/RPGSecurityTelemetrySubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/RPGPlayerState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

	bool SplitJsonDocuments(
		const FString& Contents,
		TArray<FString>& OutDocuments)
	{
		OutDocuments.Reset();
		int32 StartIndex = INDEX_NONE;
		int32 ObjectDepth = 0;
		bool bInString = false;
		bool bEscaped = false;
		for (int32 Index = 0; Index < Contents.Len(); ++Index)
		{
			const TCHAR Character = Contents[Index];
			if (bInString)
			{
				if (bEscaped)
				{
					bEscaped = false;
				}
				else if (Character == TEXT('\\'))
				{
					bEscaped = true;
				}
				else if (Character == TEXT('"'))
				{
					bInString = false;
				}
				continue;
			}

			if (Character == TEXT('"'))
			{
				if (ObjectDepth == 0)
				{
					return false;
				}
				bInString = true;
			}
			else if (Character == TEXT('{'))
			{
				if (ObjectDepth == 0)
				{
					StartIndex = Index;
				}
				++ObjectDepth;
			}
			else if (Character == TEXT('}'))
			{
				if (ObjectDepth <= 0)
				{
					return false;
				}
				--ObjectDepth;
				if (ObjectDepth == 0)
				{
					OutDocuments.Add(Contents.Mid(
						StartIndex,
						Index - StartIndex + 1));
					StartIndex = INDEX_NONE;
				}
			}
			else if (ObjectDepth == 0 && !FChar::IsWhitespace(Character))
			{
				return false;
			}
		}
		return ObjectDepth == 0 && !bInString && !bEscaped;
	}
}

void URPGSecurityTelemetrySubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bShuttingDown = false;
	bAvailable = false;
	bDeadLetterPending = false;
	RevokedMembers.Reset();
	bRevocationsDirty = false;
	Queue.SetCapacity(QueueCapacity);
	bool bEligibleServerRuntime = IsRunningDedicatedServer();
#if !UE_BUILD_SHIPPING
	const bool bDevelopmentListenServerOverride = FParse::Param(
		FCommandLine::Get(),
		TEXT("RPGSecurityTelemetryDevListenServer"));
	bEligibleServerRuntime |= bDevelopmentListenServerOverride;
#endif
	if (!bEnabled || !bEligibleServerRuntime)
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	if (bDevelopmentListenServerOverride)
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Development listen-server security telemetry override is "
				"enabled. This mode is only for local automated tests."));
	}
#endif

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
	if (!ApiUrl.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase)
		&& !ApiUrl.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry is unavailable because ApiUrl must use "
				"an http:// or https:// scheme."));
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
	RevocationPath = FPaths::Combine(SpoolDirectory, DungeonSessionId + TEXT(".revocations.json"));
	bAvailable = RestorePersistedState();
	if (!bAvailable) return;
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
	bDeadLetterPending = false;
	RevokedMembers.Reset();
	bRevocationsDirty = false;
	GameServerToken.Reset();
	DungeonSessionId.Reset();
	SpoolPath.Reset();
	DeadLetterPath.Reset();
	RevocationPath.Reset();
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
	if (IsMemberRevoked(PlayerState->GetBackendCharacterId(), SteamId)) return false;
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
		|| (Queue.HasBatchInFlight() && !bDeadLetterPending) || ActiveRequest.IsValid())
	{
		return false;
	}
	Flush();
	return ActiveRequest.IsValid();
}

bool URPGSecurityTelemetrySubsystem::IsMemberRevoked(const FString& CharacterId, const FString& SteamId) const
{
	FGuid Parsed;
	if (!FGuid::Parse(CharacterId, Parsed)) return false;
	const FString* RevokedSteam = RevokedMembers.Find(Parsed.ToString(EGuidFormats::DigitsWithHyphensLower));
	return RevokedSteam && *RevokedSteam == SteamId;
}

bool URPGSecurityTelemetrySubsystem::QuarantineRevokedDungeonMember(const FString& ExpectedSessionId,
	const FString& CharacterId, const FString& SteamId)
{
	FGuid CharacterGuid;
	if (!bAvailable || bShuttingDown || DungeonSessionId != ExpectedSessionId
		|| !FGuid::Parse(CharacterId, CharacterGuid) || !CharacterGuid.IsValid()
		|| SteamId.Len() != 17 || !RPGSecurityTelemetry::IsAsciiDigits(SteamId)) return false;
	const FString CanonicalCharacter = CharacterGuid.ToString(EGuidFormats::DigitsWithHyphensLower);
	if (const FString* Existing = RevokedMembers.Find(CanonicalCharacter); Existing && *Existing != SteamId) return false;
	if (!RevokedMembers.Contains(CanonicalCharacter) && RevokedMembers.Num() >= 128) return false;
	if (!RevokedMembers.Contains(CanonicalCharacter))
	{
		RevokedMembers.Add(CanonicalCharacter, SteamId);
		bRevocationsDirty = true;
	}
	const auto Reserved = Queue.GetReservedEvents();
	// A terminally rejected batch no longer owns an HTTP request. Keep its
	// reservation through revocation until local preservation succeeds.
	if (!bDeadLetterPending && Reserved.ContainsByPredicate([this](const FRPGSecurityTelemetryEvent& Event)
		{ return IsMemberRevoked(Event.CharacterId, Event.SteamId); }))
	{
		// A canceled mixed batch may already be committed remotely. Preserve the
		// remaining members and retry their stable EventIds; never undo backend data.
		const auto Canceled = ActiveRequest;
		ActiveRequest.Reset(); // A copied late callback cannot consume a new batch.
		if (Canceled.IsValid())
		{
			Canceled->OnProcessRequestComplete().Unbind();
			Canceled->CancelRequest();
		}
		Queue.CompleteBatch(false);
		CurrentBatchAttempt = 0;
	}
	const bool bPreserved = QuarantineRevokedPendingEvents();
	ScheduleFlush(bPreserved ? 0.f : GetRetryDelaySeconds());
	return bPreserved;
}

bool URPGSecurityTelemetrySubsystem::QuarantineRevokedPendingEvents()
{
	// The tombstone must survive a restart before evidence can leave the queue.
	if (!PersistRevocationsIfNeeded())
	{
		UE_LOG(LogRPGSecurityTelemetry, Error, TEXT("Revoked member audit quarantine could not be persisted; delivery paused."));
		return false;
	}
	const TArray<FRPGSecurityTelemetryEvent> Reserved = Queue.GetReservedEvents();
	TArray<FRPGSecurityTelemetryEvent> Pending;
	for (const auto& Event : Queue.GetEvents())
	{
		if (IsMemberRevoked(Event.CharacterId, Event.SteamId)
			&& !Reserved.ContainsByPredicate([&Event](const auto& Entry) { return Entry.EventId == Event.EventId; }))
			Pending.Add(Event);
	}
	// Never delete evidence if the durable quarantine cannot be written.
	for (const auto& Event : Pending)
	{
		if (!AppendEventToFile(Event, DeadLetterPath))
		{
			UE_LOG(LogRPGSecurityTelemetry, Error, TEXT("Revoked member audit quarantine could not be persisted; delivery paused."));
			return false;
		}
	}
	for (const auto& Event : Pending) Queue.RemovePendingMemberEvents(Event.CharacterId, Event.SteamId);
	if (!Pending.IsEmpty())
	{
		DeliveryFailureDropCount += Pending.Num();
		// If rewriting fails, the original spool still preserves evidence. Durable
		// tombstones filter it again before delivery after a restart.
		if (!PersistQueueSnapshot())
			UE_LOG(LogRPGSecurityTelemetry, Error, TEXT("Audit quarantine preserved, but queue spool rewrite failed."));
	}
	return true;
}

void URPGSecurityTelemetrySubsystem::ScheduleFlush(const float DelaySeconds)
{
	if (!bAvailable || bShuttingDown || Queue.Num() == 0
		|| (Queue.HasBatchInFlight() && !bDeadLetterPending) || ActiveRequest.IsValid())
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	const float RequestedDelay = FMath::Max(0.01f, DelaySeconds);
	if (Timers.TimerExists(FlushTimerHandle))
	{
		// New observations must neither bypass retry backoff nor postpone an
		// already scheduled flush. A healthy full batch may flush earlier.
		if (CurrentBatchAttempt > 0 || bDeadLetterPending
			|| Timers.GetTimerRemaining(FlushTimerHandle) <= RequestedDelay)
		{
			return;
		}
	}
	Timers.SetTimer(
		FlushTimerHandle,
		this,
		&URPGSecurityTelemetrySubsystem::Flush,
		RequestedDelay,
		false);
}

void URPGSecurityTelemetrySubsystem::Flush()
{
	if (!bAvailable || bShuttingDown || ActiveRequest.IsValid())
	{
		return;
	}
	if (!QuarantineRevokedPendingEvents())
	{
		ScheduleFlush(GetRetryDelaySeconds());
		return;
	}
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UWorld* World = GameInstance->GetWorld())
		{
			// FlushNow can run before the timer; cancel that old deadline so it
			// cannot interfere with the next acknowledgement or retry schedule.
			World->GetTimerManager().ClearTimer(FlushTimerHandle);
		}
	}

	if (bDeadLetterPending)
	{
		// The terminal response was already received. Retry only the local
		// evidence write; do not POST this rejected batch again in this process.
		FinishPendingDeadLetter();
		return;
	}

	// A legacy spool may predate the durable local revocation file. Wait for
	// this world's authenticated membership read and quarantine exact immutable
	// receipts before reserving ANY event for a new delivery batch.
	if (Queue.Num() == 0) return;
	if (!PrepareMembershipDelivery())
	{
		ScheduleFlush(GetRetryDelaySeconds());
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
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry batch could not be encoded; local preservation required."));
		bDeadLetterPending = true;
		FinishPendingDeadLetter();
		return;
	}

	CurrentBatchAttempt = FMath::Min(
		CurrentBatchAttempt + 1,
		MaximumAttempts);
	FHttpRequestRef Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ApiUrl + TEXT("/security/events/batch"));
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
		UE_LOG(LogRPGSecurityTelemetry, Warning,
			TEXT("Security telemetry retry scheduled: HTTP=0 Attempt=%d Delay=%.2fs Queued=%d."),
			CurrentBatchAttempt, GetRetryDelaySeconds(), Queue.Num());
		ScheduleFlush(GetRetryDelaySeconds());
	}
}

void URPGSecurityTelemetrySubsystem::HandleRequestComplete(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	const bool bWasSuccessful)
{
	if (!Request.IsValid() || Request != ActiveRequest)
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
	if (bPermanentFailure)
	{
		const FString ResponseDetail = Response.IsValid()
			? RPGSecurityTelemetry::SanitizeDetail(
				Response->GetContentAsString())
			: TEXT("No HTTP response");
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry rejected after HTTP %d and %d attempt(s); local preservation required: %s"),
			StatusCode,
			CurrentBatchAttempt,
			*ResponseDetail);
		bDeadLetterPending = true;
		FinishPendingDeadLetter();
		return;
	}
	if (!bAcknowledged)
	{
		UE_LOG(LogRPGSecurityTelemetry, Warning,
			TEXT("Security telemetry retry scheduled: HTTP=%d Attempt=%d Delay=%.2fs Queued=%d."),
			StatusCode, CurrentBatchAttempt, GetRetryDelaySeconds(), Queue.Num());
	}

	Queue.CompleteBatch(bAcknowledged);
	if (bAcknowledged)
	{
		CurrentBatchAttempt = 0;
		PersistQueueSnapshot();
	}
	ScheduleFlush(
		bAcknowledged ? 0.0f : GetRetryDelaySeconds());
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

bool URPGSecurityTelemetrySubsystem::RestorePersistedState()
{
	if (!LoadPersistedRevocations())
	{
		UE_LOG(LogRPGSecurityTelemetry, Error, TEXT("Security telemetry revocation recovery failed; delivery disabled and spool preserved."));
		return false;
	}
	LoadPersistedEvents();
	return true;
}

bool URPGSecurityTelemetrySubsystem::LoadPersistedRevocations()
{
	if (!bPersistToDisk) return true;
	if (RevocationPath.IsEmpty()) return false;
	TMap<FString, FString> Recovered;
	bool bHasPendingSnapshot = false;
	// A valid unfinished rename may contain newer tombstones than the main file.
	// Merge both; any corrupt/conflicting file disables delivery rather than guessing.
	for (const FString& Path : {RevocationPath, RevocationPath + TEXT(".tmp")})
	{
		if (!IFileManager::Get().FileExists(*Path)) continue;
		const int64 Size = IFileManager::Get().FileSize(*Path);
		if (Size <= 0 || Size > 65536) return false;
		FString Contents;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Contents, *Path)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Contents), Root) || !Root) return false;
		double Version = 0;
		FString Session;
		const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
		if (!Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1
			|| !Root->TryGetStringField(TEXT("dungeonSessionId"), Session) || Session != DungeonSessionId
			|| !Root->TryGetArrayField(TEXT("members"), Members) || Members->Num() > 128) return false;
		TSet<FString> Seen;
		for (const auto& Value : *Members)
		{
			const TSharedPtr<FJsonObject>* Member = nullptr;
			FString Character, Steam;
			FGuid ParsedCharacter;
			if (!Value || !Value->TryGetObject(Member) || !Member || !Member->IsValid()
				|| !(*Member)->TryGetStringField(TEXT("characterId"), Character)
				|| !FGuid::Parse(Character, ParsedCharacter) || !ParsedCharacter.IsValid()
				|| !(*Member)->TryGetStringField(TEXT("steamId"), Steam)
				|| Steam.Len() != 17 || !RPGSecurityTelemetry::IsAsciiDigits(Steam)) return false;
			Character = ParsedCharacter.ToString(EGuidFormats::DigitsWithHyphensLower);
			if (Seen.Contains(Character)) return false;
			Seen.Add(Character);
			if (const auto* Existing = Recovered.Find(Character); Existing && *Existing != Steam) return false;
			Recovered.Add(Character, Steam);
			if (Recovered.Num() > 128) return false;
		}
		bHasPendingSnapshot |= Path.EndsWith(TEXT(".tmp"));
	}
	RevokedMembers = MoveTemp(Recovered);
	bRevocationsDirty = bHasPendingSnapshot;
	return PersistRevocationsIfNeeded();
}

bool URPGSecurityTelemetrySubsystem::PersistRevocationsIfNeeded()
{
	if (!bRevocationsDirty) return true;
	if (!bPersistToDisk) { bRevocationsDirty = false; return true; }
	if (RevocationPath.IsEmpty()
		|| !IFileManager::Get().MakeDirectory(*FPaths::GetPath(RevocationPath), true)) return false;
	auto Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("schemaVersion"), 1);
	Root->SetStringField(TEXT("dungeonSessionId"), DungeonSessionId);
	TArray<FString> Characters;
	RevokedMembers.GetKeys(Characters);
	Characters.Sort();
	TArray<TSharedPtr<FJsonValue>> Members;
	for (const auto& Character : Characters)
	{
		auto Member = MakeShared<FJsonObject>();
		Member->SetStringField(TEXT("characterId"), Character);
		Member->SetStringField(TEXT("steamId"), RevokedMembers.FindChecked(Character));
		Members.Add(MakeShared<FJsonValueObject>(Member));
	}
	Root->SetArrayField(TEXT("members"), Members);
	FString Contents;
	const FString TemporaryPath = RevocationPath + TEXT(".tmp");
	if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Contents))
		|| !FFileHelper::SaveStringToFile(Contents, *TemporaryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		|| !IFileManager::Get().Move(*RevocationPath, *TemporaryPath, true, true, false, true)) return false;
	bRevocationsDirty = false;
	return true;
}

void URPGSecurityTelemetrySubsystem::LoadPersistedEvents()
{
	if (!bPersistToDisk || SpoolPath.IsEmpty()
		|| !IFileManager::Get().FileExists(*SpoolPath))
	{
		return;
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *SpoolPath))
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry could not read spool %s."),
			*SpoolPath);
		return;
	}
	TArray<FString> Documents;
	if (!RPGSecurityTelemetry::SplitJsonDocuments(Contents, Documents))
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Error,
			TEXT("Security telemetry spool %s is structurally invalid; "
				"the original file was preserved."),
			*SpoolPath);
		return;
	}

	int32 RecoveredCount = 0;
	int32 InvalidDocumentCount = 0;
	for (const FString& Document : Documents)
	{
		FString PersistedSessionId;
		TArray<FRPGSecurityTelemetryEvent> PersistedEvents;
		if (!FRPGSecurityTelemetryJsonCodec::DeserializeBatch(
			Document,
			PersistedSessionId,
			PersistedEvents)
			|| !PersistedSessionId.Equals(
				DungeonSessionId,
				ESearchCase::IgnoreCase))
		{
			++InvalidDocumentCount;
			continue;
		}
		for (FRPGSecurityTelemetryEvent& Event : PersistedEvents)
		{
			RecoveredCount += Queue.Enqueue(MoveTemp(Event)) ? 1 : 0;
		}
	}
	// Restore/filter tombstones before rewriting the recovered spool or scheduling POST.
	if (!QuarantineRevokedPendingEvents()) return;
	PersistQueueSnapshot();
	if (InvalidDocumentCount > 0)
	{
		UE_LOG(
			LogRPGSecurityTelemetry,
			Warning,
			TEXT("Security telemetry recovered %d event(s); invalid spool documents=%d."),
			RecoveredCount,
			InvalidDocumentCount);
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

bool URPGSecurityTelemetrySubsystem::FinishPendingDeadLetter()
{
	if (!IsInGameThread() || !bAvailable || bShuttingDown || !bDeadLetterPending
		|| !Queue.HasBatchInFlight() || ActiveRequest.IsValid()) return false;
	if (!AppendReservedBatchToDeadLetter())
	{
		UE_LOG(LogRPGSecurityTelemetry, Warning,
			TEXT("Security telemetry preservation failed; reserved evidence retained and only local storage will retry."));
		ScheduleFlush(GetRetryDelaySeconds());
		return false;
	}
	const int32 PreservedCount = Queue.GetReservedCount();
	Queue.CompleteBatch(true);
	bDeadLetterPending = false;
	DeliveryFailureDropCount += PreservedCount;
	CurrentBatchAttempt = 0;
	if (!PersistQueueSnapshot())
	{
		// Durable dead-letter evidence already exists. Preserve the old spool on
		// failure; a restart may replay stable EventIds, never invent a receipt.
		UE_LOG(LogRPGSecurityTelemetry, Warning,
			TEXT("Security telemetry dead-letter preserved, but spool rewrite failed; original spool retained."));
	}
	ScheduleFlush(0.0f);
	return true;
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
