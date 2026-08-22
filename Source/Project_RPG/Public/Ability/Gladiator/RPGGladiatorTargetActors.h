#pragma once

#include "Abilities/GameplayAbilityTargetActor_Trace.h"
#include "RPGGladiatorTargetActors.generated.h"

/** D1-compatible line trace target actor that highlights the actor under the reticle. */
UCLASS()
class PROJECT_RPG_API ARPGGameplayAbilityTargetActor_LineTraceHighlight
	: public AGameplayAbilityTargetActor_Trace
{
	GENERATED_BODY()

public:
	ARPGGameplayAbilityTargetActor_LineTraceHighlight(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual FHitResult PerformTrace(AActor* InSourceActor) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void HighlightActor(bool bShouldHighlight, AActor* ActorToHighlight);

	UPROPERTY()
	TWeakObjectPtr<AActor> CachedTracedActor;
};
