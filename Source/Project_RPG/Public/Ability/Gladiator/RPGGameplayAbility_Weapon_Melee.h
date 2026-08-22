#pragma once

#include "Ability/Gladiator/RPGGameplayAbility_Equipment.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "RPGGameplayAbility_Weapon_Melee.generated.h"

class UAnimMontage;

/**
 * Shared D1-compatible melee damage pipeline.
 *
 * Target data may contain either a character hit or a hit on equipment owned by a
 * character. Both forms are normalized to the target's ASC avatar before team,
 * duplicate-hit, and blocking checks are performed.
 */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGGameplayAbility_Weapon_Melee : public URPGGameplayAbility_Equipment
{
	GENERATED_BODY()

public:
	URPGGameplayAbility_Weapon_Melee(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Melee")
	void ParseTargetData(const FGameplayAbilityTargetDataHandle& InTargetDataHandle,
		TArray<int32>& OutCharacterHitIndexes, TArray<int32>& OutBlockHitIndexes);

	/** Historical D1 signature retained for imported ability Blueprints. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Melee")
	void ProcessHitResult(FHitResult HitResult, float Damage, bool bBlockingHit,
		UAnimMontage* BackwardMontage, AActor* WeaponActor);

	UFUNCTION(BlueprintCallable, Category = "RPG|Gladiator|Melee")
	void ResetHitActors();

	UFUNCTION(BlueprintPure, Category = "RPG|Gladiator|Melee")
	bool IsCharacterBlockingHit(AActor* TargetActor) const;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Direct-hit entry point used by temporary overlap traces until Phase 3. */
	bool TryProcessHitResult(const FHitResult& HitResult, float Damage,
		bool bBlockingHit, UAnimMontage* BackwardMontage, AActor* WeaponActor);

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee",
		meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BlockingAngle = 60.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee",
		meta = (ClampMin = "0.0"))
	float BlockHitDamageMultiplier = 0.3f;

	/** Defense-in-depth range check applied again immediately before server damage. */
	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee|Security",
		meta = (ClampMin = "1.0", Units = "cm"))
	float MaximumServerHitDistance = 1200.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee|Security",
		meta = (ClampMin = "0.0", Units = "cm"))
	float ServerHitLocationTolerance = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee|Security",
		meta = (ClampMin = "1.0"))
	float MaximumServerDamagePerHit = 10000000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "RPG|Gladiator|Melee|Debug")
	bool bShowDebug = false;

private:
	AActor* ResolveTargetActor(const FHitResult& HitResult) const;
	bool IsHostileTarget(const AActor* TargetActor) const;
	void ExecuteImpactCue(const FHitResult& HitResult, bool bBlockingHit) const;
	void DrawDebugHitPoint(const FHitResult& HitResult) const;

	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> CachedHitActors;
};
