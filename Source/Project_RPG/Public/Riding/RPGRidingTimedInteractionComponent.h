#pragma once
#include "Components/ActorComponent.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingTimedInteractionComponent.generated.h"
class APawn;
class APlayerState;
class URPGRidingComponent;
class FRPGRidingTimedInteractionLease;

/** Owns one cancellable input and one server observation. No client duration or state setter. */
UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingTimedInteractionComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGRidingTimedInteractionComponent();
    bool Begin(AActor& InTarget, TFunction<void(float)> Ready, TFunction<void()> Complete, TFunction<void()> Interrupted);
    bool Finish();
    void Cancel();
    bool HasRequest() const { return Local.Sequence != 0; }
    bool IsActive() const { return Phase == EPhase::Active; }
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingTimedInteractionTestAccess;
#endif
    enum class EPhase : uint8 { None, Starting, Active, Finishing };
    struct FContext
    {
        uint32 Sequence = 0;
        TWeakObjectPtr<APawn> Pawn;
        TWeakObjectPtr<APlayerState> PlayerState;
        TWeakObjectPtr<AActor> Target;
        TWeakObjectPtr<URPGRidingComponent> Riding;
        FGuid Identity;
        FRPGRidingStateKey Key;
        float Duration = 0;
        double Started = 0, Expires = 0;
    };
    bool Prepare(AActor& InTarget, TFunction<void(float)> Ready, TFunction<void()> Complete, TFunction<void()> Interrupted);
    bool Matches(const FContext& Context, bool bServer, bool bExactState) const;
    void PollLocal();
    void PollServer();
    void ClearLocal();
    void EndLocal(bool bSuccess);
    void StopServer(uint32 InSequence, bool bNotify);
    void UpdateTick();
    UFUNCTION(Server, Reliable) void ServerBegin(uint32 InSequence, APawn* InPawn, AActor* InTarget,
        FGuid InIdentity, FRPGRidingStateKey InExpected);
    UFUNCTION(Server, Reliable) void ServerFinish(uint32 InSequence);
    UFUNCTION(Server, Reliable) void ServerCancel(uint32 InSequence);
    UFUNCTION(Client, Reliable) void ClientStarted(uint32 InSequence, bool bAccepted, FRPGRidingStateKey InKey, float InDuration);
    UFUNCTION(Client, Reliable) void ClientFinished(uint32 InSequence, bool bSuccess, FRPGRidingStateKey InKey);
    FContext Local, Server;
    FRPGRidingStateKey Requested;
    EPhase Phase = EPhase::None;
    uint32 LocalSequence = 0, ServerSequence = 0;
    double LocalDeadline = 0, NextServerRequest = 0;
    bool bPreparing = false, bEnding = false, bStartAck = false, bFinishAck = false, bRequestedMounted = false;
    TFunction<void(float)> OnReady;
    TFunction<void()> OnComplete, OnInterrupted;
    TSharedPtr<FRPGRidingTimedInteractionLease> Lease;
};
