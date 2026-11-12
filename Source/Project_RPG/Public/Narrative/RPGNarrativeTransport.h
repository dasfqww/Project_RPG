#pragma once
#include "CoreMinimal.h"
#include "Narrative/RPGNarrativeTypes.h"
#include "Narrative/RPGNarrativeTransportInterface.h"
#include "Interfaces/IHttpRequest.h"

class UHttpWebManager;

/** Player credentials remain inside UHttpWebManager. One active request per transport instance. */
class PROJECT_RPG_API FRPGHttpNarrativeTransport final : public IRPGNarrativeTransport,
    public TSharedFromThis<FRPGHttpNarrativeTransport>
{
public:
    explicit FRPGHttpNarrativeTransport(UHttpWebManager* InWeb);
    virtual ~FRPGHttpNarrativeTransport() override;
    virtual bool GetContext(FString& Endpoint, FString& SteamId) const override;
    virtual bool Send(FGuid CharacterId, const FString& ChoiceBody, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual bool SendDungeon(FGuid CharacterId, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual bool SendHistory(FGuid CharacterId, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual bool SendSkipPreview(FGuid CharacterId, const FString& Chapter, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual bool SendSkip(FGuid CharacterId, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Completion) override;
    virtual void Cancel() override;
private:
    bool SendRequest(FGuid CharacterId, const FString& Body, bool bDungeon, TFunction<void(FRPGNarrativeHttpResult)> Completion,
        bool bHistory = false, const FString& Suffix = FString());
    TWeakObjectPtr<UHttpWebManager> Web;
    FHttpRequestPtr ActiveRequest;
    uint64 Generation = 0;
};
