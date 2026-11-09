#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HttpFwd.h"
#include "Narrative/RPGNarrativeKill.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RPGNarrativeKillSubsystem.generated.h"

class ARPGNonPlayerCharacter;

/** Server outbox owns observations after the victim actor disappears. No client submission RPC. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeKillSubsystem final : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    /** Explicit authored policy: confirmed session members with a current pawn in this radius at death. */
    bool RecordDeathInRadius(ARPGNonPlayerCharacter* Victim, const FString& EnemyId, float Radius);
private:
    struct FPending
    {
        FRPGNarrativeKillAttempt Attempt;
        FString Steam, Endpoint, Server;
        TWeakObjectPtr<UWorld> World;
        FHttpRequestPtr Request;
        double NextRetry = 0;
        uint64 RequestGeneration = 0;
    };
    bool Tick(float DeltaSeconds);
    bool ContextMatches(const FPending& Pending) const;
    void Dispatch(FGuid Key);
    void Receive(FGuid Key, uint64 Generation, FRPGNarrativeInteractionHttpResult Result);
    void Cancel(FGuid Key);
    TMap<FGuid, FPending> Outbox;
    TSet<TWeakObjectPtr<ARPGNonPlayerCharacter>> RecordedVictims;
    FTSTicker::FDelegateHandle Ticker;
};
