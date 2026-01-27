// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "RPGNPCDropItemAbility.generated.h"

class URPGItemBase;
class ARPGPickUpBase;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGNPCDropItemAbility : public URPGNPCGameplayAbility
{
	GENERATED_BODY()
public:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	void SpawnItem();



protected:
	UPROPERTY(EditDefaultsOnly, Category = "Item Drop Table")
	TObjectPtr<UDataTable> ItemDropTable;
	
	UPROPERTY(EditDefaultsOnly, Category = "Item Drop Table")
	FName DropRowName;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TSubclassOf<ARPGPickUpBase> PickupClass;
};
