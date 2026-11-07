#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "RPGRidingActionBarSubsystem.generated.h"

class APawn;
class URPGRidingComponent;
class URPGActionBarContext;
class FRPGRidingActionBarProvider;
struct FRPGRidingRepState;

/** Local projection of approved Riding state. Independent of the visual HUD lifetime. */
UCLASS()
class PROJECT_RPG_API URPGRidingActionBarSubsystem final : public ULocalPlayerSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void PlayerControllerChanged(APlayerController* NewController) override;
private:
    UFUNCTION() void HandlePawnChanged(APawn* OldPawn, APawn* NewPawn);
    void HandleRidingChanged(const FRPGRidingRepState& State);
    void RefreshPawn();
    void DropPawn();
    void Reconcile();
    bool WatchBinding(float DeltaSeconds);
    bool IsControllerCurrent() const;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<APawn> Pawn;
    TWeakObjectPtr<URPGRidingComponent> Riding;
    TWeakObjectPtr<URPGActionBarContext> Context;
    TSharedPtr<FRPGRidingActionBarProvider> Provider;
    FDelegateHandle RidingHandle, ContextHandle;
    FTSTicker::FDelegateHandle WatchHandle;
    uint64 BindingRevision = 0;
    bool bEnding = false;
    bool bReconciling = false;
};
