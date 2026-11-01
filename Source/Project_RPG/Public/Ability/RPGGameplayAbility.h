// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Combat/RPGApprovedHitContext.h"
#include "Combat/RPGKnockbackTypes.h"
#include "Combat/RPGSlowTypes.h"
#include "Combat/RPGBurnTypes.h"
#include "Combat/RPGDefenseReductionTypes.h"
#include "Combat/RPGDamageTakenIncreaseTypes.h"
#include "Combat/RPGStaggerTypes.h"
#include "Combat/RPGCombatImmunityTypes.h"

#include "CoreMinimal.h"
#include "Combat/RPGHitReactionTypes.h"
#include "Abilities/GameplayAbility.h"
#include "Type/RPGEnumTypes.h"
#include "RPGGameplayAbility.generated.h"

class UPawnCombatComponent;
class URPGAbilitySystemComponent;
class URPGAttributeSet;
class ARPGPlayerController;
class UTexture2D;
class URPGCameraMode;
class URPGCombatControlComponent;

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
	/** Server-local execution identity; never a client-issued immunity permit. */
	FGuid GetCombatExecutionId() const { return CombatExecutionId; }
	bool CanUseMontageImmunity() const;
	/** Class grant transactions cannot execute one-shot/passive callbacks before the plan is committed. */
	bool ActivatesOnGrant() const
	{
		return AbilityActivationPolicy == ERPGAbilityActivationPolicy::OnGiven
			|| ActivationPolicy == ERPGGladiatorAbilityActivationPolicy::OnSpawn;
	}

	/** End a montage interruption as cancelled without recursively broadcasting cancellation. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Security|Movement")
	void EndInterruptedMovementAbility();

	/** Lets an ability publish prediction data before GAS forwards input RPCs. */
	virtual bool PreReplicateAbilityInputPressed() { return true; }
	virtual bool PreReplicateAbilityInputReleased() { return true; }

protected:
	FGuid CombatExecutionId;
	FRPGApprovedHitContext CombatExecutionLifetime;
	int32 CombatExecutionBossEncounter = 0;
	bool bMontageImmunityReady = false;
	/** Skill containers defer protection until their frozen configuration is committed. */
	virtual bool AutomaticallyApplyActivationImmunity() const { return true; }
	bool StartActivationImmunity(const FRPGCombatImmunitySettings& Settings);
	FRPGCombatImmunitySettings ResolveActivationImmunity(const FGameplayAbilitySpec* Spec) const;
	bool IsHitReactionAbility() const;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPGAbility|Combat")
	FRPGCombatImmunitySettings ActivationImmunities;
	FGuid ActivationImmunityToken;
	TWeakObjectPtr<URPGCombatControlComponent> ActivationImmunityOwner;
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

	/** Client-requested input abilities are limited; passives and server reactions are exempt. */
	bool ShouldApplyServerInputEnforcement() const;
	bool ShouldApplyServerActivationRateLimit() const;

	UPROPERTY(EditDefaultsOnly, Category = "RPGAbility")
		ERPGAbilityActivationPolicy AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnTriggered;

	UPROPERTY(EditDefaultsOnly, Category = "RPGAbility|Security")
	bool bCountTowardServerActivationRateLimit = true;

	/** Refresh for this invocation; a shared ability CDO must never retain an owner's attributes. */
	const URPGAttributeSet* RefreshAttributeSetFromActorInfo(const FGameplayAbilityActorInfo* ActorInfo);

	UPROPERTY(Transient)
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
			(AActor* TargetActor, const FGameplayEffectSpecHandle& InSpecHandle, ERPGSuccessType& OutSuccessType,
			 bool& bOutUseLegacyHitReaction);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Warrior|Ability")
	void ApplyGameplayEffectSpecHandleToHitResults(const FGameplayEffectSpecHandle& InSpecHandle,
			const TArray<FHitResult>& InHitResults);

	/** Native post-approval routing. The policy is frozen before damage callbacks.
	 * True consumes only the legacy event, including when cosmetic playback is skipped. */
	ERPGAttackHitReactionPolicy GetGrantedHitReactionPolicy() const;
	float GetGrantedStunDuration() const;
	FRPGKnockbackSettings GetGrantedKnockback() const;
	FRPGSlowSettings GetGrantedSlow() const;
	FRPGBurnSettings GetGrantedBurn() const;
	FRPGDefenseReductionSettings GetGrantedDefenseReduction() const;
	FRPGDamageTakenIncreaseSettings GetGrantedDamageTakenIncrease() const;
	FRPGStaggerSettings GetGrantedStagger() const;
	bool TryRouteApprovedHitReaction(const FHitResult& ServerHit, ERPGAttackHitReactionPolicy Policy, float StunDuration,
		const FRPGKnockbackSettings& Knockback, const FRPGSlowSettings& Slow, const FRPGApprovedHitContext& HitContext, const FRPGBurnSettings& Burn = FRPGBurnSettings(), const FRPGStaggerSettings& Stagger = FRPGStaggerSettings(), const FRPGDefenseReductionSettings& DefenseReduction = FRPGDefenseReductionSettings(), const FRPGDamageTakenIncreaseSettings& DamageTakenIncrease = FRPGDamageTakenIncreaseSettings());

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
