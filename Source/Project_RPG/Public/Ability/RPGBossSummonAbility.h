#pragma once

#include "Ability/RPGBossGameplayAbility.h"
#include "AI/RPGBossSummonComponent.h"
#include "TimerManager.h"
#include "RPGBossSummonAbility.generated.h"

/** Windup/commit hand off an atomic wave to the encounter-owned registry, then release the BT. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGBossSummonAbility : public URPGBossGameplayAbility
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon") FGameplayTag ActionTag;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon") TSubclassOf<ARPGNonPlayerCharacter> SummonClass;
	/** Capsule-center offsets, relative to the boss's position/yaw captured when the cast commits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon") TArray<FVector> LocalSpawnOffsets;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.01", ClampMax = "120")) float WindupSeconds = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.1", ClampMax = "300")) float SummonLifetime = 30.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0.1", ClampMax = "30")) float StartupTimeout = 10.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "1", ClampMax = "32")) int32 MaximumOwnedSummons = 8;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon") bool bRequireNavigation = true;
	/** Disable for boss-anchored phase mechanics that must survive aggro changes.
	 * Still requires the same living encounter, boss Controller and ASC context.
	 * True preserves existing combat summons; false never captures a player target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Boss|Summon") bool bRequireThreatTarget = true;
protected:
	virtual bool RequiresExecutionTarget() const override { return bRequireThreatTarget; }
	virtual bool CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
	UPROPERTY(Transient) FRPGBossSummonPlan LockedPlan;
	TWeakObjectPtr<URPGBossSummonComponent> Registry;
	FTimerHandle CastTimer;
	int32 LockedSequence = 0;
	int32 LockedEncounter = 0;
	bool bReleased = false;
	bool MakePlan(const APawn* Source, FRPGBossSummonPlan& OutPlan) const;
	void ReleaseSummons(uint64 Serial);
};
