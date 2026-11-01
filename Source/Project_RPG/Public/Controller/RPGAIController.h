// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RPGAIController.generated.h"

class UAIPerceptionComponent;
class UAISenseConfig_Sight;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGAIController : public AAIController
{
	GENERATED_BODY()
public:
	ARPGAIController(const FObjectInitializer& ObjectInitializer);

	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const;
	virtual void Tick(float DeltaSeconds) override;
	virtual FPathFollowingRequestResult MoveTo(const FAIMoveRequest& Request, FNavPathSharedPtr* OutPath = nullptr) override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
		TObjectPtr<UAIPerceptionComponent> NPCPerceptionComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
		TObjectPtr<UAISenseConfig_Sight> AISenseConfig_Sight;

	UFUNCTION()
		virtual void OnNPCPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

private:
	/** Keep encounter-owned ordinary NPCs on a living encounter participant.
	 * Returns true when the summon path owns TargetActor instead of perception. */
	bool RefreshEncounterSummonTarget();

	UPROPERTY(EditDefaultsOnly, Category = "Detour Crowd Avoidance Config")
		bool bEnableDetourCrowdAvoidance = true;

	UPROPERTY(EditDefaultsOnly, Category = "Detour Crowd Avoidance Config", meta = (EditCondition = bEnableDetourCrowdAvoidance, UIMin="1", UIMax="4"))
		int32 DetourCrowdAvoidanceQuality = 4;

	UPROPERTY(EditDefaultsOnly, Category = "Detour Crowd Avoidance Config", meta = (EditCondition = bEnableDetourCrowdAvoidance, UIMin = "1", UIMax = "4"))
		float CollisionQueryRange = 600.f;
};
