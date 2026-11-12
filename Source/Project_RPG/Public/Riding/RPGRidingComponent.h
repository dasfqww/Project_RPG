#pragma once

#include "Component/PawnExtensionComponentBase.h"
#include "Riding/RPGRidingTypes.h"
#include "Riding/RPGRidingDashAdmission.h"
#include "Riding/RPGRidingDashGrant.h"
#include "Riding/RPGRidingEmoteAdmission.h"
#include "GameplayAbilitySpecHandle.h"
#include "RPGRidingComponent.generated.h"

class URPGMountDefinition;
class URPGRidingSessionComponent;
class URPGPlayerMovementComponent;
class UAbilitySystemComponent;
struct FRPGRidingDecision;
struct FRPGRidingEvaluationContext;
class FRPGRidingDashRuntime;
class FRPGRidingDashPrediction;
struct FRPGRidingDashIntent;
struct FRPGRidingMoveStamp;
struct FRPGRidingDashRuntimeDeleter { void operator()(FRPGRidingDashRuntime*) const; };
struct FRPGRidingDashPredictionDeleter { void operator()(FRPGRidingDashPrediction*) const; };
DECLARE_MULTICAST_DELEGATE_OneParam(FRPGRidingChanged, const FRPGRidingRepState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRPGRidingResultReceived, const FRPGRidingCommandResult&);

UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingComponent final : public UPawnExtensionComponentBase
{
    GENERATED_BODY()
public:
    URPGRidingComponent();
    virtual ~URPGRidingComponent() override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void OnRegister() override;
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION(BlueprintPure, Category="Riding") FRPGRidingRepState GetRidingState() const { return State; }
    UFUNCTION(BlueprintCallable, Category="Riding") void RequestSetMounted(bool bMounted, const FRPGRidingSelector& Selector);
    /** Requests only the current source-owned GAS spec; GAS performs the owning-client RPC. */
    UFUNCTION(BlueprintCallable, Category="Riding") bool RequestDash();
    UFUNCTION(BlueprintPure, Category="Riding") bool CanRequestDash() const;
    UFUNCTION(BlueprintCallable, Category="Riding") bool RequestEmote();
    UFUNCTION(BlueprintPure, Category="Riding") bool CanRequestEmote() const;
    /** Read-only approved profile; requires current owner/avatar/source spec and mount generation. */
    bool TryGetDashGrant(FRPGRidingDashGrant& Out) const;
    /** Movement-only projection. This never replaces the replicated authority state. */
    FRPGRidingRepState GetDashMovementState() const;
    bool IsDashMovementActive() const;
    float GetPredictedDashCooldownRemaining() const;
    void PrepareDashPredictionMovement();
    void FinalizeDashPredictionMovement();
    double ConstrainPredictedDashMovement(const FVector& Start, const FVector& End);
    void RecordPredictedDashMovement(FGuid Execution, const FVector& Start, const FVector& End);
    /** Native server boundary. Never trusts BP authority metadata or a client-supplied reason. */
    void ForceDismount(ERPGRidingEndReason Reason);
    void RefreshBinding();
    FRPGRidingChanged OnChanged;
    FRPGRidingResultReceived OnResult;
    /** Current cosmetic definition only; cannot grant ownership or mutate the authoritative state. */
    const URPGMountDefinition* GetPresentationDefinition() const;
    bool TryBuildMovementProfile(FRPGRidingMovementProfile& Out) const;
    /** Server movement entrypoints revalidate guards before simulating an accepted move. */
    void ValidateServerMovementContext();
    /** Server CMC: fraction of a proposed capsule segment permitted while mounted; may end Riding. */
    double ConstrainServerMovement(const FVector& Start, const FVector& End);
    /** Native server executor. Admission may be cancelled by callbacks; query active motion before creating FX. */
    ERPGRidingResultCode StartServerDash(FRPGRidingStateKey Expected, FVector Direction, FRPGRidingDashActivation Activation = {});
    bool IsServerDashActive() const;
    void PrepareServerDashMovement();
    void FinalizeServerDashMovement();
    void RecordServerDashMovement(FGuid ExecutionId, const FVector& Start, const FVector& End);
    void StopServerDashForControl();

private:
    friend class FRPGRidingInteractionScope;
    friend class URPGRidingSessionComponent;
    friend class URPGPlayerMovementComponent;
    friend class URPGGameplayAbility_RidingDash;
    friend class URPGGameplayAbility_RidingEmote;
    friend class FRPGRidingDashRuntime;
    friend class FRPGRidingDashPrediction;
    friend class FSavedMove_RPGRiding;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingActorTestAccess;
    friend struct FRPGRidingMovementTestAccess;
    friend struct FRPGRidingDashTestAccess;
    friend struct FRPGRidingDashAbilityTestAccess;
    friend struct FRPGRidingDashGrantTestAccess;
    friend struct FRPGRidingDashPredictionTestAccess;
    friend struct FRPGRidingEmoteTestAccess;
#endif
    UFUNCTION() void HandleControllerChanged(APawn* Pawn, AController* Old, AController* Current);
    UFUNCTION() void HandleRestarted(APawn* Pawn);
    UFUNCTION() void OnRep_State(const FRPGRidingRepState& Previous);
    UFUNCTION(Server, Reliable) void ServerSetRiding(FRPGRidingSetCommand Command);
    UFUNCTION(Client, Reliable) void ClientRidingResult(FRPGRidingCommandResult Result);
    bool IsBindingCurrent() const;
    /** Trusted native executor preflight only. No client callback, RPC, BP or activation grant. */
    ERPGRidingResultCode TryBeginDash(FRPGRidingStateKey Expected, FVector Direction,
        TFunctionRef<bool(const FRPGRidingDashProfile&, const FVector&)> Preflight, FRPGRidingDashTicket& Out, FRPGRidingDashActivation Activation = {});
    ERPGRidingResultCode EvaluateDashAdmission(const FRPGRidingStateKey& Expected) const;
    bool IsDashCurrent(const FRPGRidingDashTicket& Ticket) const;
    bool MatchesDash(const FRPGRidingDashTicket& Ticket) const;
    bool FinishDash(const FRPGRidingDashTicket& Ticket, ERPGRidingEndReason Reason);
    void SyncDashAbility();
    void ReleaseDashAbility();
    bool OwnsDashAbility(const UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle) const;
    bool IsDashActivationCurrent(const FRPGRidingDashActivation& Activation) const;
    bool IsDashOwnerTimingReady() const;
    FGameplayAbilitySpecHandle FindDashAbility(UAbilitySystemComponent*& OutASC) const;
    void SyncEmoteAbility();
    void ReleaseEmoteAbility();
    bool OwnsEmoteAbility(const UAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Handle) const;
    FGameplayAbilitySpecHandle FindEmoteAbility(UAbilitySystemComponent*& OutASC) const;
    ERPGRidingResultCode EvaluateEmoteAdmission() const;
    bool BeginEmote(FRPGRidingEmoteTicket& Out);
    bool MatchesEmote(const FRPGRidingEmoteTicket& Ticket) const;
    bool FinishEmote(const FRPGRidingEmoteTicket& Ticket, ERPGRidingEndReason Reason);
    void UpdateEmote();
    void ReleaseBinding();
    void Publish();
    bool Commit(const FRPGRidingDecision& Decision);
    bool ApplyMovementCorrection(const FRPGRidingRepState& Incoming);
    void MakeOnFoot(ERPGRidingEndReason Reason);
    void BuildGuards(FRPGRidingEvaluationContext& Out) const;
    bool IsRegionAllowed(const URPGMountDefinition& Definition) const;
    const URPGMountDefinition* FindDefinition(const FString& Id) const;
    TWeakObjectPtr<URPGRidingSessionComponent> Session;
    TWeakObjectPtr<AController> Controller;
    FGuid LocalRequestEpoch;
    uint32 LocalRequestSeq = 0;
    bool bEnding = false, bApplying = false;
    bool bPreparingDash = false;
    bool bSyncingDashAbility = false;
    TWeakObjectPtr<UAbilitySystemComponent> DashAbilityOwner;
    FGameplayAbilitySpecHandle DashAbilityHandle;
    FRPGRidingStateKey DashGrantKey;
    FRPGRidingDashTicket ActiveDash;
    double DashDeadline = 0;
    bool bSyncingEmoteAbility = false;
    TWeakObjectPtr<UAbilitySystemComponent> EmoteAbilityOwner;
    FGameplayAbilitySpecHandle EmoteAbilityHandle;
    FRPGRidingStateKey EmoteAbilityKey;
    FRPGRidingEmoteTicket ActiveEmote;
    double EmoteDeadline = 0;
    TUniquePtr<FRPGRidingDashRuntime, FRPGRidingDashRuntimeDeleter> DashRuntime;
    TUniquePtr<FRPGRidingDashPrediction, FRPGRidingDashPredictionDeleter> DashPrediction;
    ERPGRidingEndReason PendingEnd = ERPGRidingEndReason::None;
    UPROPERTY(EditDefaultsOnly, Category="Riding") TArray<TObjectPtr<URPGMountDefinition>> Definitions;
    UPROPERTY(ReplicatedUsing=OnRep_State) FRPGRidingRepState State;
    UPROPERTY(ReplicatedUsing=OnRep_DashGrant) FRPGRidingDashGrant DashGrant;
    UFUNCTION() void OnRep_DashGrant();
};
