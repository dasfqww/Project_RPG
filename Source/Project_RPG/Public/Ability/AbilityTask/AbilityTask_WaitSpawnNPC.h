// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_WaitSpawnNPC.generated.h"

class ARPGNonPlayerCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWaitSpawnNPCDelegate, const TArray<ARPGNonPlayerCharacter*>&, SpawnedNPCs);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UAbilityTask_WaitSpawnNPC : public UAbilityTask
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityTasks", meta = 
		(DisplayName = "Wait Gameplay Event And Spawn NPC", HidePin = "OwningAbility",
			DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "true",
			NumToSpawn = "1", RandomSpawnRadius = "200"))
	static UAbilityTask_WaitSpawnNPC* WaitSpawnNPCs(
		UGameplayAbility* OwningAbility,
		FGameplayTag EventTag,
		TSoftClassPtr<ARPGNonPlayerCharacter> SoftNPCClassToSpawn,
		int32 NumToSpawn,
		const FVector& SpawnOrigin,
		float RandomSpawnRadius
	);

	UPROPERTY(BlueprintAssignable)
	FWaitSpawnNPCDelegate OnSpawnFinished;

	UPROPERTY(BlueprintAssignable)
	FWaitSpawnNPCDelegate DidNotSpawn;

	//~ Begin UGameplayTask Interface
	virtual void Activate() override;
	virtual void OnDestroy(bool bInOwnerFinished) override;
	//~ End UGameplayTask Interface

private:
	FGameplayTag CachedEventTag;
	TSoftClassPtr<ARPGNonPlayerCharacter> CachedSoftNPCClassToSpawn;
	int32 CachedNumToSpawn;
	FVector CachedSpawnOrigin;
	float CachedRandomSpawnRadius;
	FDelegateHandle DelegateHandle;

	void OnGameplayEventReceived(const FGameplayEventData* InPayload);
	void OnNPCClassLoaded();
};
