#pragma once
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGInteractionInputTestTarget.generated.h"

class UBoxComponent;

/** Editor-only target for the interaction input regression; never grants items. */
UCLASS(Transient, NotBlueprintable)
class ARPGInteractionInputTestTarget : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGInteractionInputTestTarget();
    UFUNCTION(BlueprintCallable) bool ConfigureForTest(float Duration, FText Label);
    UFUNCTION(BlueprintPure) int32 GetBeginCount() const { return BeginCount; }
    UFUNCTION(BlueprintPure) int32 GetCancelCount() const { return CancelCount; }
    UFUNCTION(BlueprintPure) int32 GetCompleteCount() const { return CompleteCount; }
    virtual void BeginInteract() override;
    virtual void EndInteract() override;
    virtual void Interact(APlayerController* Controller) override;
private:
    UPROPERTY() TObjectPtr<UBoxComponent> Trigger;
    bool bConfigured = false, bInteracting = false;
    int32 BeginCount = 0, CancelCount = 0, CompleteCount = 0;
};
