#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HttpFwd.h"
#include "Narrative/RPGNarrativeInteraction.h"
#include "RPGNarrativeInvestigationComponent.generated.h"

class ARPGNarrativeInvestigation;
class ARPGPlayerState;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPGInvestigationStatus, const FText&, Status, bool, bCanRetry);

/** Owned controller RPC boundary; world investigation actors are never assigned to a requesting client. */
UCLASS(ClassGroup=(Narrative), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGNarrativeInvestigationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGNarrativeInvestigationComponent();
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool Investigate(ARPGNarrativeInvestigation* Target);
    UFUNCTION(BlueprintCallable, Category="RPG|Narrative") bool RetryPendingInvestigation();
    UPROPERTY(BlueprintAssignable, Category="RPG|Narrative") FRPGInvestigationStatus OnStatus;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    UFUNCTION(Server, Reliable) void ServerInvestigate(ARPGNarrativeInvestigation* Target, FGuid ActionId);
    UFUNCTION(Client, Reliable) void ClientInvestigationResult(FGuid ActionId, FGuid CharacterId, FGuid SessionId, bool bSuccess, bool bRetryable);
    bool ServerContextMatches() const;
    void Dispatch();
    void Receive(uint64 Generation, FRPGNarrativeInteractionHttpResult Result);
    void Status(const FText& Text, bool bRetry);
    bool ClientContextMatches() const;
    FRPGNarrativeInteractionAttempt Attempt;
    FHttpRequestPtr ActiveRequest;
    TWeakObjectPtr<ARPGPlayerState> ApprovedState;
    TWeakObjectPtr<ARPGNarrativeInvestigation> ServerTarget, LocalTarget;
    FString ApprovedSteam, ApprovedServer, ApprovedEndpoint, LocalSteam, LocalEndpoint;
    FGuid ServerAction, LocalAction, LocalCharacter, LocalSession;
    struct FCompletedAction { FGuid Action, Character, Session; bool bSuccess; };
    TArray<FCompletedAction> CompletedActions;
    double NextRetry = 0, NextApproval = 0, NextClientRetry = 0;
    uint64 RequestGeneration = 0;
    bool bRefreshNeeded = false;
};
