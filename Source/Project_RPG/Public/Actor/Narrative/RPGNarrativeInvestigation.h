#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/InteractionInterface.h"
#include "RPGNarrativeInvestigation.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Instant personal investigation. Only the server reads TargetId and approves the observation. */
UCLASS()
class PROJECT_RPG_API ARPGNarrativeInvestigation : public AActor, public IInteractionInterface
{
    GENERATED_BODY()
public:
    ARPGNarrativeInvestigation();
    virtual void Interact(APlayerController* Controller) override;
    virtual void BeginFocus() override;
    virtual void EndFocus() override;
    bool CanObserve(APlayerController* Controller) const;
    const FString& GetTargetId() const { return TargetId; }
    void SetLocalStatus(const FText& Status, bool bRetry);
protected:
    virtual void BeginPlay() override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Investigation") TObjectPtr<USphereComponent> InteractionBounds;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Investigation") TObjectPtr<UStaticMeshComponent> Appearance;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Investigation") TObjectPtr<UTextRenderComponent> Nameplate;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative") FString TargetId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative") FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RPG|Narrative", meta=(ClampMin="50", ClampMax="1000")) float InteractionDistance = 250.f;
};
