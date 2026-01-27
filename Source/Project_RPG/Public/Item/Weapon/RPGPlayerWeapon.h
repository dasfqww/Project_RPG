// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Item/Weapon/RPGWeaponBase.h"
#include "Type/RPGStructTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "RPGPlayerWeapon.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayerWeapon : public ARPGWeaponBase
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WeaponData")
		FRPGPlayerWeaponData PlayerWeaponData;

	UFUNCTION(BlueprintCallable)
		void AssignGrantedAbilitySpecHandles(const TArray<FGameplayAbilitySpecHandle>& InSpecHandles);

	UFUNCTION(BlueprintPure)
		TArray<FGameplayAbilitySpecHandle> GetGrantedAbilitySpecHandles() const;

private:
	TArray<FGameplayAbilitySpecHandle> GrantedAbilitySpecHandles;
};
