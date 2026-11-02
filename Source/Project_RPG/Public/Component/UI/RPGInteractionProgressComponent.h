#pragma once
#include "Components/ActorComponent.h"
#include "UI/Progress/RPGActionProgressTypes.h"
#include "RPGInteractionProgressComponent.generated.h"

class UPlayerUIComponent;
class APawn;

/** Presentation producer only. The controller still owns interaction completion/cancellation. */
UCLASS()
class PROJECT_RPG_API URPGInteractionProgressComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGInteractionProgressComponent();
    void BeginInteraction(APawn* Pawn, AActor* Target, const FText& Label, float Duration);
    void UpdateInteraction(float DeltaSeconds);
    void EndInteraction(ERPGActionProgressEndReason Reason);
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void OnPawnChanged(APawn* OldPawn, APawn* NewPawn);
    UFUNCTION() void OnTargetEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    TWeakObjectPtr<UPlayerUIComponent> UI;
    TWeakObjectPtr<APawn> SourcePawn;
    TWeakObjectPtr<AActor> SourceTarget;
    FRPGActionProgressHandle Handle;
    FRPGActionProgressData Data;
    uint64 Revision = 0;
};
