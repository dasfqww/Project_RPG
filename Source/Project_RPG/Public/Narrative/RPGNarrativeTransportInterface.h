#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"

/** Transport contract independent from the HTTP implementation and its credentials. */
class PROJECT_RPG_API IRPGNarrativeTransport
{
public:
    virtual ~IRPGNarrativeTransport() = default;
    virtual bool GetContext(FString& Endpoint, FString& SteamId) const = 0;
    virtual bool Send(FGuid CharacterId, const FString& ChoiceBody, TFunction<void(FRPGNarrativeHttpResult)> Completion) = 0;
    virtual bool SendDungeon(FGuid CharacterId, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) = 0;
    virtual bool SendHistory(FGuid, TFunction<void(FRPGNarrativeHttpResult)>) { return false; }
    virtual void Cancel() = 0;
};
