#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "Interfaces/IHttpRequest.h"

class UHttpWebManager;

class PROJECT_RPG_API IRPGNarrativeTransport
{
public:
    virtual ~IRPGNarrativeTransport() = default;
    virtual bool GetContext(FString& Endpoint, FString& SteamId) const = 0;
    virtual bool Send(FGuid CharacterId, const FString& ChoiceBody, TFunction<void(FRPGNarrativeHttpResult)> Completion) = 0;
    virtual void Cancel() = 0;
};

/** Player credentials remain inside UHttpWebManager. One request per local coordinator. */
class PROJECT_RPG_API FRPGHttpNarrativeTransport final : public IRPGNarrativeTransport,
    public TSharedFromThis<FRPGHttpNarrativeTransport>
{
public:
    explicit FRPGHttpNarrativeTransport(UHttpWebManager* InWeb);
    virtual ~FRPGHttpNarrativeTransport() override;
    virtual bool GetContext(FString& Endpoint, FString& SteamId) const override;
    virtual bool Send(FGuid CharacterId, const FString& ChoiceBody, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual void Cancel() override;
private:
    TWeakObjectPtr<UHttpWebManager> Web;
    FHttpRequestPtr ActiveRequest;
    uint64 Generation = 0;
};
