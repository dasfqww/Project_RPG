// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Type/RPGStructTypes.h"
#include "RPGAbilitySystemComponent.generated.h"

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
public:
	void OnAbilityInputPressed(const FGameplayTag& InInputTag);
	void OnAbilityInputReleased(const FGameplayTag& InInputTag);
	void OnAbilitySpecInputPressed(FGameplayAbilitySpecHandle SpecHandle);
	void OnAbilitySpecInputReleased(FGameplayAbilitySpecHandle SpecHandle);

	/**
	 * Resolves one granted ability by its ability tag.
	 * Ambiguous matches intentionally fail instead of activating a random spec.
	 */
	FGameplayAbilitySpecHandle FindUniqueAbilitySpecHandleByTag(
		const FGameplayTag& AbilityTag);

	bool IsAbilitySpecInputPressed(FGameplayAbilitySpecHandle SpecHandle) const;

	/** Narrow compatibility surface required by the Gladiator cooldown-duration execution. */
	FActiveGameplayEffect* GetActiveGameplayEffectMutable(FActiveGameplayEffectHandle Handle);
	TArray<FActiveGameplayEffectHandle> GetAllActiveEffectHandles() const;
	void MarkActiveGameplayEffectDirty(FActiveGameplayEffect* ActiveGameplayEffect);
	void CheckActiveEffectDuration(FActiveGameplayEffectHandle Handle);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability", meta=(ApplyLevel="1"))
		void GrantPlayerWeaponAbilities(const TArray<FRPGPlayerAbilitySet> InDefaultWeaponAbilities, 
			const TArray<FRPGPlayerSkillSet>& InSkillAbilities,
			int32 ApplyLevel, TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability")
		void RemovedGrantedPlayerWeaponAbilities(UPARAM(ref) TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove);

	UFUNCTION(BlueprintCallable, Category = "RPG|Ability")
		bool TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate);

protected:
	virtual void AbilitySpecInputPressed(FGameplayAbilitySpec& Spec) override;
	virtual void AbilitySpecInputReleased(FGameplayAbilitySpec& Spec) override;
};
