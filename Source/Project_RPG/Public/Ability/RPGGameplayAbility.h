// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameplayAbility.generated.h"

class UPawnCombatComponent;
class URPGAbilitySystemComponent;
class URPGAttributeSet;

UENUM(BlueprintType)
enum class ERPGAbilityActivationPolicy : uint8
{
	OnTriggered,
	OnGiven
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

protected:
	//~ Begin UGameplayAbility Interface.
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, 
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	//~ End UGameplayAbility Interface

	UPROPERTY(EditDefaultsOnly, Category = "RPGAbility")
		ERPGAbilityActivationPolicy AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnTriggered;

	UPROPERTY()
	const URPGAttributeSet* CachedAttributeSet;

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	UPawnCombatComponent* GetPawnCombatComponentFromActorInfo() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		URPGAbilitySystemComponent* GetRPGAbilitySystemComponentFromActorInfo() const;
	
	FActiveGameplayEffectHandle NativeApplyEffectSpecHandleToTarget
		(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability", meta =
		(DisplayName = "Apply Gameplay Effect Spec Handle To Target Actor ", ExpandEnumAsExecs = "OutSuccessType"))
		FActiveGameplayEffectHandle BP_ApplyEffectSpecHandleToTarget
			(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType);

	UFUNCTION(BlueprintCallable, Category = "Warrior|Ability")
	void ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle,
			const TArray<FHitResult>& InHitResults);

	//void DisplayDamageEffect(AActor* InCachedTargetActor, float InWeaponBaseDamage, bool bCritical);

	void DisplayInvincibleEffect(AActor* InCachedTargetActor);
};