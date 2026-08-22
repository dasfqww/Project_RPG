// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameplayAbility.generated.h"

class UPawnCombatComponent;
class URPGAbilitySystemComponent;
class URPGAttributeSet;
class ARPGPlayerController;
class UTexture2D;
class URPGCameraMode;

UENUM(BlueprintType)
enum class ERPGAbilityActivationPolicy : uint8
{
	OnTriggered,
	OnGiven
};

/** Serialized activation policy used by imported Lyra/D1 ability assets. */
UENUM(BlueprintType)
enum class ERPGGladiatorAbilityActivationPolicy : uint8
{
	Manual,
	OnInputTriggered,
	WhileInputActive,
	OnSpawn
};

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API URPGGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** Lets an ability publish prediction data before GAS forwards input RPCs. */
	virtual bool PreReplicateAbilityInputPressed() { return true; }
	virtual bool PreReplicateAbilityInputReleased() { return true; }

protected:
	//~ Begin UGameplayAbility Interface.
	virtual bool CanActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr,
		const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, 
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	
	virtual void OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	//~ End UGameplayAbility Interface

	/** Client-predicted input abilities are limited; passives and server reactions are exempt. */
	bool ShouldApplyServerActivationRateLimit() const;

	UPROPERTY(EditDefaultsOnly, Category = "RPGAbility")
		ERPGAbilityActivationPolicy AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnTriggered;

	UPROPERTY(EditDefaultsOnly, Category = "RPGAbility|Security")
	bool bCountTowardServerActivationRateLimit = true;

	UPROPERTY()
	const URPGAttributeSet* CachedAttributeSet;

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
	UPawnCombatComponent* GetPawnCombatComponentFromActorInfo() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Ability")
		URPGAbilitySystemComponent* GetRPGAbilitySystemComponentFromActorInfo() const;

	/** Kept under its original Blueprint name so imported widget abilities relink. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Ability")
	ARPGPlayerController* GetLyraPlayerControllerFromActorInfo() const;
	
	FActiveGameplayEffectHandle NativeApplyEffectSpecHandleToTarget
		(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|Ability", meta =
		(DisplayName = "Apply Gameplay Effect Spec Handle To Target Actor ", ExpandEnumAsExecs = "OutSuccessType"))
		FActiveGameplayEffectHandle BP_ApplyEffectSpecHandleToTarget
			(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Warrior|Ability")
	void ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle,
			const TArray<FHitResult>& InHitResults);

	//void DisplayDamageEffect(AActor* InCachedTargetActor, float InWeaponBaseDamage, bool bCritical);

	void DisplayInvincibleEffect(AActor* InCachedTargetActor);

public:
	UTexture2D* GetAbilityIcon() const { return Icon; }
	const FText& GetAbilityDisplayName() const { return Name; }
	const FText& GetAbilityDescription() const { return Description; }

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gladiator|Ability")
	ERPGGladiatorAbilityActivationPolicy ActivationPolicy = ERPGGladiatorAbilityActivationPolicy::OnInputTriggered;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gladiator|Ability")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gladiator|Ability")
	FText Name;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gladiator|Ability")
	FText Description;

	/** Imported equipment abilities serialize their temporary camera mode under this name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gladiator|Camera")
	TSubclassOf<URPGCameraMode> CameraModeClass;
};
