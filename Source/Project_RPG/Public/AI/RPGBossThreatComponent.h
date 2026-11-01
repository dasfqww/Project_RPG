#pragma once

#include "Components/ActorComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "GameplayEffectTypes.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGBossThreatComponent.generated.h"

class APawn;
class AController;
class AAIController;

/** Server-only threat ledger. Blackboard is a mirror, never the source of target selection. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossThreatComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossThreatComponent();

	/** Capture the encounter ID when the attack is created, not when its delayed hit arrives. No RPC. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Threat")
	bool AddThreat(APawn* Target, float Amount, int32 ExpectedEncounterId);

	/** Server GAS/encounter hook for taunts and scripted focus, not a client RPC.
	 * Caller owns skill range/LOS, commit and cooldown. Last accepted request wins.
	 * Returns a lock token, or zero. Does not retarget an already captured ability execution.
	 * Duration is 0.01..120s; loss of eligibility, phase/reset/death releases the lock. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Threat")
	int32 ForceTarget(APawn* Target, float DurationSeconds, int32 ExpectedEncounterId);

	/** Only the matching encounter/token can release a lock; old ability callbacks are harmless. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Threat")
	bool ReleaseForcedTarget(int32 ExpectedEncounterId, int32 ExpectedLockToken);

	/** Server-only live override, or null when expired/ineligible. Not a replicated UI snapshot. */
	UFUNCTION(BlueprintPure, Category = "Boss|Threat")
	APawn* GetForcedTarget() const;

	/**
	 * Opt-in incoming damage path. Call once per server-confirmed hit, instead of applying the GE separately.
	 * Capture ExpectedEncounterId when creating the attack. Instant RPG damage specs only (no DOT).
	 * Returns GE application success, not health loss; lethal hits clear threat through the boss lifecycle.
	 * The caller owns trace/LOS, ability commit, execution cancellation and duplicate-hit prevention. No RPC.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Damage")
	bool ApplyDamageFromServerHit(APawn* Source, const FHitResult& ServerHit,
		const FGameplayEffectSpecHandle& DamageSpec, int32 ExpectedEncounterId,
		const FRPGSkillSecurityProfile& SecurityProfile, float& OutHealthDamage, bool& bOutThreatAdded);

	UFUNCTION(BlueprintPure, Category = "Boss|Threat")
	APawn* GetCurrentTarget() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Threat")
	double GetThreatForTarget(APawn* Target) const;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Threat")
	void RefreshTargets();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Threat")
	bool ClearThreat();

	/** Called by the RPG controller for sight events only; repeated sightings do not add score. */
	void NotifySightTarget(APawn* Target, bool bSensed);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Threat", meta = (ClampMin = "1.0", ClampMax = "1000000.0"))
	float MaxTargetDistance = 6000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Threat", meta = (ClampMin = "0.01"))
	float InitialSightThreat = 1.0f;

	/** A challenger must strictly exceed current threat multiplied by this value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Threat", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float TargetSwitchMultiplier = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Threat", meta = (ClampMin = "1", ClampMax = "512"))
	int32 MaxTrackedTargets = 128;

	/** Object key accepting Pawn/Actor. Leave None to disable the Blackboard mirror. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Threat")
	FName TargetActorKey = TEXT("TargetActor");

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FThreatEntry
	{
		double Score = 0.0;
		uint64 Order = 0;
		TWeakObjectPtr<AController> Controller;
	};
	TWeakObjectPtr<URPGBossAIComponent> Boss;
	TWeakObjectPtr<APawn> CurrentTarget;
	TWeakObjectPtr<APawn> ForcedTarget;
	TWeakObjectPtr<AController> ForcedTargetController;
	TWeakObjectPtr<AController> ForcedSourceController;
	double ForcedTargetDeadline = 0.0;
	int32 ForcedEncounterId = 0;
	int32 ForcedPhaseIndex = INDEX_NONE;
	int32 LastLockToken = 0;
	int32 ActiveLockToken = 0;
	bool bCurrentTargetForced = false;
	FTimerHandle ForcedTargetTimeout;
	TWeakObjectPtr<AAIController> LastBlackboardController;
	TMap<TWeakObjectPtr<APawn>, FThreatEntry> Entries;
	TSet<TWeakObjectPtr<APawn>> PerceivedTargets;
	uint64 NextOrder = 0;
	bool bUpdating = false;
	bool bApplyingDamage = false;
	bool bEndingPlay = false;

	bool IsServer() const;
	bool IsCombatActive() const;
	bool IsEligible(APawn* Target) const;
	FThreatEntry* AdmitTarget(APawn* Target);
	void SelectTarget();
	void WriteBlackboardTarget();
	void ClearForcedTargetState();

	UFUNCTION()
	void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState);
};
