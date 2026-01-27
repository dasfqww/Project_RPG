// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "NPC_HitReaction.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API UNPC_HitReaction : public URPGNPCGameplayAbility
{
	GENERATED_BODY()
public:
	UNPC_HitReaction();

protected:
	

private:
	UPROPERTY(EditDefaultsOnly, Category = "React", meta = (AllowPrivateAccess = "true"))
	bool bFaceAttacker;
	
	UPROPERTY(EditDefaultsOnly, Category = "React", meta = (AllowPrivateAccess = "true"))
	bool bHasHitReactMontagesToPlay;

	UPROPERTY(EditDefaultsOnly, Category = "React", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UAnimMontage> MontageToPlay;

	UPROPERTY(EditDefaultsOnly, Category = "React", meta = (AllowPrivateAccess = "true"))
	TArray<FName> StartSections;
};
