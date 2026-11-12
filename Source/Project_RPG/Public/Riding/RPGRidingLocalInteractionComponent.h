#pragma once
#include "Components/ActorComponent.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingLocalInteractionComponent.generated.h"

class APlayerController;
class APlayerState;
class APawn;
class URPGRidingComponent;

/** Owner-only UI continuation. A server receipt never writes client Riding state:
 * the matching replicated on-foot state must also arrive before invoking the callback. */
UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingLocalInteractionComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGRidingLocalInteractionComponent();
    static bool NeedsApproval(const APlayerController* Controller);
    bool Request(AActor& Target, TFunction<void()> Continue);
    bool IsPending() const { return PendingSequence != 0; }
    void Cancel();
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingLocalInteractionTestAccess;
#endif
    bool Prepare(AActor& Target, TFunction<void()> Continue);
    bool Matches() const;
    void Poll();
    UFUNCTION(Server, Reliable) void ServerPrepare(uint32 Sequence, APawn* ExpectedPawn,
        AActor* InTarget, FGuid InIdentity, FRPGRidingStateKey InExpected);
    UFUNCTION(Client, Reliable) void ClientPrepared(uint32 Sequence, bool bAccepted, FRPGRidingStateKey InApproved);
    uint32 LocalSequence = 0, PendingSequence = 0, ServerSequence = 0;
    bool bPreparing = false, bEnding = false, bApproved = false;
    double Deadline = 0, NextServerRequest = 0;
    TWeakObjectPtr<APawn> Pawn;
    TWeakObjectPtr<APlayerState> PlayerState;
    TWeakObjectPtr<AActor> Target;
    TWeakObjectPtr<URPGRidingComponent> Riding;
    FGuid Identity;
    FRPGRidingStateKey Expected, Approved;
    TFunction<void()> Continuation;
};
