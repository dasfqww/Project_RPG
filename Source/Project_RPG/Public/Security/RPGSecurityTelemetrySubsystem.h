#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Security/RPGSecurityTelemetryTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "RPGSecurityTelemetrySubsystem.generated.h"

class AActor;
class IHttpRequest;
class IHttpResponse;

/**
 * Dedicated-server bridge from authoritative security observations to the
 * authenticated backend audit store.
 */
UCLASS(Config = Game, DefaultConfig)
class PROJECT_RPG_API URPGSecurityTelemetrySubsystem final
	: public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	bool EnqueueViolation(
		const AActor* ObservedActor,
		const FRPGSecurityViolation& Violation,
		float RiskAfter);

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Telemetry")
	bool IsAvailable() const { return bAvailable; }

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Telemetry")
	int32 GetQueuedEventCount() const { return Queue.Num(); }

	UFUNCTION(BlueprintPure, Category = "RPG|Security|Telemetry")
	int64 GetDroppedEventCount() const
	{
		return Queue.GetDroppedCount() + DeliveryFailureDropCount;
	}

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|Security|Telemetry")
	bool FlushNow();

private:
	void ScheduleFlush(float DelaySeconds);
	void Flush();
	void HandleRequestComplete(
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bWasSuccessful);
	void HandlePostWorldInitialization(
		UWorld* World,
		const UWorld::InitializationValues InitializationValues);
	void LoadPersistedEvents();
	bool AppendEventToFile(
		const FRPGSecurityTelemetryEvent& Event,
		const FString& FilePath) const;
	bool AppendReservedBatchToDeadLetter() const;
	bool PersistQueueSnapshot() const;
	float GetRetryDelaySeconds() const;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry")
	bool bEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry")
	FString ApiUrl = TEXT("http://localhost:3000/api");

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "0.1", Units = "s"))
	float FlushIntervalSeconds = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 BatchSize = 32;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "64", ClampMax = "8192"))
	int32 QueueCapacity = 512;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "1.0", Units = "s"))
	float RequestTimeoutSeconds = 10.0f;

	/** Transient failures keep retrying; this only caps backoff escalation. */
	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "1", ClampMax = "10",
			DisplayName = "Backoff Ramp Attempts"))
	int32 MaximumAttempts = 4;

	/** Keeps unacknowledged evidence across graceful shutdowns and restarts. */
	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry")
	bool bPersistToDisk = true;

	UPROPERTY(Config, EditAnywhere, Category = "Security Telemetry",
		meta = (ClampMin = "1.0", ClampMax = "120.0", Units = "s"))
	float MaximumRetryDelaySeconds = 30.0f;

	bool bAvailable = false;
	bool bShuttingDown = false;
	int32 CurrentBatchAttempt = 0;
	int64 DeliveryFailureDropCount = 0;
	FString GameServerToken;
	FString DungeonSessionId;
	FString SpoolPath;
	FString DeadLetterPath;
	FRPGSecurityTelemetryQueue Queue;
	FTimerHandle FlushTimerHandle;
	FDelegateHandle PostWorldInitializationHandle;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
};
