#pragma once

#include "CoreMinimal.h"

/** Frozen server observation. Retries must preserve every field, including the timestamp. */
struct PROJECT_RPG_API FRPGNarrativeInteractionCommand
{
    FGuid SessionId, CharacterId, EventId;
    FString TargetId;
    int64 ConnectionGeneration = 0;
    FDateTime ObservedAt;
    bool IsValid() const;
    FString Encode() const;
    static bool IsValidId(const FString& Id);
};

struct PROJECT_RPG_API FRPGNarrativeInteractionHttpResult
{
    bool bCurrentServerContext = false;
    bool bTransportSuccessful = false;
    int32 StatusCode = 0;
    FString Body;
};

enum class ERPGNarrativeInteractionOutcome : uint8 { Unknown, Succeeded, Rejected };

/** One unresolved server event; even a malformed 200 may already have committed. */
class PROJECT_RPG_API FRPGNarrativeInteractionAttempt
{
public:
    bool Begin(const FRPGNarrativeInteractionCommand& Observation);
    bool IsPending() const { return Pending.IsSet(); }
    const FRPGNarrativeInteractionCommand* GetCommand() const { return Pending ? &Pending.GetValue() : nullptr; }
    const FString& GetBody() const { return Body; }
    bool StartRequest();
    ERPGNarrativeInteractionOutcome Receive(const FRPGNarrativeInteractionHttpResult& Result);
    bool CanAutoRetry() const { return IsPending() && !bInFlight && Attempts < 4; }
    float RetryDelay() const { return static_cast<float>(1 << FMath::Clamp(Attempts - 1, 0, 2)); }
    void AllowManualRetry() { if (!bInFlight) Attempts = 0; }
    void Reset();
    static bool DecodeReceipt(const FString& Json, const FRPGNarrativeInteractionCommand& Expected);
private:
    TOptional<FRPGNarrativeInteractionCommand> Pending;
    FString Body;
    int32 Attempts = 0;
    bool bInFlight = false;
};
