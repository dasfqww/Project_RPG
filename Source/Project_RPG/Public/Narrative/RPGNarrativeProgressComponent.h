#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RPGNarrativeProgressComponent.generated.h"

/** Owner-only invalidation notice. The client obtains all actual progress through its authenticated GET. */
UCLASS()
class PROJECT_RPG_API URPGNarrativeProgressComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGNarrativeProgressComponent();
    void NotifyCommitted(FGuid Character, FGuid Session);
protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
    UFUNCTION(Client, Reliable) void ClientCommitted(FGuid Character, FGuid Session);
    bool ContextMatches() const;
    FGuid CharacterId, SessionId;
    FString Steam, Endpoint;
    double NextRefresh = 0;
};
