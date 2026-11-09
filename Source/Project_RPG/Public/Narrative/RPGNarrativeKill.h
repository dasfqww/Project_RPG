#pragma once

#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeInteraction.h"

/** One server-approved death/recipient pair. Never rebuild it from a retry's current state. */
struct PROJECT_RPG_API FRPGNarrativeKillCommand
{
    FGuid SessionId, CharacterId, EventId;
    FString EnemyId;
    int64 ConnectionGeneration = 0;
    FDateTime ObservedAt;
    bool IsValid() const;
    FString Encode() const;
};

class PROJECT_RPG_API FRPGNarrativeKillAttempt
{
public:
    bool Begin(const FRPGNarrativeKillCommand& Command);
    bool StartRequest();
    ERPGNarrativeInteractionOutcome Receive(const FRPGNarrativeInteractionHttpResult& Result);
    bool IsPending() const { return Pending.IsSet(); }
    bool IsInFlight() const { return bInFlight; }
    float RetryDelay() const { return static_cast<float>(FMath::Min(30, 1 << FMath::Clamp(Attempts - 1, 0, 5))); }
    const FRPGNarrativeKillCommand* GetCommand() const { return Pending ? &Pending.GetValue() : nullptr; }
    const FString& GetBody() const { return Body; }
    static bool DecodeReceipt(const FString& Json, const FRPGNarrativeKillCommand& Expected);
private:
    TOptional<FRPGNarrativeKillCommand> Pending;
    FString Body;
    int32 Attempts = 0;
    bool bInFlight = false;
};

/** Actor lifetime only. Startup, destruction and retirement are not kills. */
struct PROJECT_RPG_API FRPGNarrativeKillLife
{
    void Arm(bool bServerReady, float Health, bool bDead);
    bool Consume(bool bServerReady, float Health, bool bDead);
private:
    bool bArmed = false;
    bool bConsumed = false;
};
