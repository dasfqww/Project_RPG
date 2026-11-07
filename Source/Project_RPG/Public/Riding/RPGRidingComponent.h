#pragma once

#include "Component/PawnExtensionComponentBase.h"
#include "Riding/RPGRidingTypes.h"
#include "RPGRidingComponent.generated.h"

class URPGMountDefinition;
class URPGRidingSessionComponent;
struct FRPGRidingDecision;
struct FRPGRidingEvaluationContext;
DECLARE_MULTICAST_DELEGATE_OneParam(FRPGRidingChanged, const FRPGRidingRepState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRPGRidingResultReceived, const FRPGRidingCommandResult&);

UCLASS(ClassGroup=(RPG))
class PROJECT_RPG_API URPGRidingComponent final : public UPawnExtensionComponentBase
{
    GENERATED_BODY()
public:
    URPGRidingComponent();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void OnRegister() override;
    virtual void TickComponent(float Delta, ELevelTick Tick, FActorComponentTickFunction* Function) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UFUNCTION(BlueprintPure, Category="Riding") FRPGRidingRepState GetRidingState() const { return State; }
    UFUNCTION(BlueprintCallable, Category="Riding") void RequestSetMounted(bool bMounted, const FRPGRidingSelector& Selector);
    /** Native server boundary. Never trusts BP authority metadata or a client-supplied reason. */
    void ForceDismount(ERPGRidingEndReason Reason);
    void RefreshBinding();
    FRPGRidingChanged OnChanged;
    FRPGRidingResultReceived OnResult;
    /** Current cosmetic definition only; cannot grant ownership or mutate the authoritative state. */
    const URPGMountDefinition* GetPresentationDefinition() const;

private:
    friend class URPGRidingSessionComponent;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FRPGRidingActorTestAccess;
#endif
    UFUNCTION() void HandleControllerChanged(APawn* Pawn, AController* Old, AController* Current);
    UFUNCTION() void HandleRestarted(APawn* Pawn);
    UFUNCTION() void OnRep_State(const FRPGRidingRepState& Previous);
    UFUNCTION(Server, Reliable) void ServerSetRiding(FRPGRidingSetCommand Command);
    UFUNCTION(Client, Reliable) void ClientRidingResult(FRPGRidingCommandResult Result);
    bool IsBindingCurrent() const;
    void ReleaseBinding();
    void Publish();
    void Commit(const FRPGRidingDecision& Decision);
    void MakeOnFoot(ERPGRidingEndReason Reason);
    void BuildGuards(FRPGRidingEvaluationContext& Out) const;
    const URPGMountDefinition* FindDefinition(const FString& Id) const;
    TWeakObjectPtr<URPGRidingSessionComponent> Session;
    TWeakObjectPtr<AController> Controller;
    FGuid LocalRequestEpoch;
    uint32 LocalRequestSeq = 0;
    bool bEnding = false, bApplying = false;
    ERPGRidingEndReason PendingEnd = ERPGRidingEndReason::None;
    UPROPERTY(EditDefaultsOnly, Category="Riding") TArray<TObjectPtr<URPGMountDefinition>> Definitions;
    UPROPERTY(ReplicatedUsing=OnRep_State) FRPGRidingRepState State;
};
