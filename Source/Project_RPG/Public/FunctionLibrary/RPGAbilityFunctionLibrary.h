// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Type/RPGEnumTypes.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "RPGAbilityFunctionLibrary.generated.h"

class URPGAbilitySystemComponent;
struct FRPGSkillSecurityProfile;
struct FScalableFloat;
struct FGameplayEffectSpecHandle;

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGAbilityFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	static URPGAbilitySystemComponent* NativeGetWarriorASCFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityFunctionLibrary")
		static void AddGameplayTagToActorIfNone(AActor* InActor, FGameplayTag TagToAdd);

	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityFunctionLibrary")
		static void RemoveGameplayTagFromActorIfFound(AActor* InActor, FGameplayTag TagToRemove);

	static bool NativeDoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck);

	UFUNCTION(BlueprintCallable, Category = "RPG|AbilityFunctionLibrary", meta=(DisplayName="Does Actor Have Tag", ExpandEnumAsExecs="OutConfirmType"))
		static void BP_DoesActorHaveTag(AActor* InActor, FGameplayTag TagToCheck, ERPGConfirmType& OutConfirmType);

	UFUNCTION(BlueprintPure, Category = "RPG|AbilityFunctionLibrary", meta = (CompactNodeTitle = "Get Value At Level"))
		static float GetScalableFloatValueAtLevel(const FScalableFloat& InScalableFloat, float InLevel = 1.f);

	/**
	 * Legacy BP compatibility entry point. Damage-tagged specs are automatically
	 * routed through server hit and damage validation before application.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|AbilityFunctionLibrary")
		static bool ApplyGameplayEffectSpecHandleToTargetActor(AActor* InInstigator, AActor* InTargetActor, 
			const FGameplayEffectSpecHandle& InSpecHandle);

	/** Native path for server-owned traces and projectile collisions. */
	static bool ApplyGameplayEffectSpecHandleToServerHit(
		AActor* InInstigator,
		const FHitResult& ServerHit,
		const FGameplayEffectSpecHandle& InSpecHandle,
		const FRPGSkillSecurityProfile& SecurityProfile,
		FActiveGameplayEffectHandle* OutActiveHandle = nullptr);
};
