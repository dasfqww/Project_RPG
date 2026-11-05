#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AI/RPGBossPatternDefinition.h"
#include "RPGBossDefinition.generated.h"

class URPGBossGameplayAbility;

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossPhaseDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	FGameplayTag PhaseTag;

	/** First phase must be 1.0; subsequent thresholds must strictly decrease. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HealthThreshold = 1.0f;

	/** Default priority order. Select the first already-granted ability allowed by GAS.
	 * Empty preserves legacy BTs that select an explicit AbilityClass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TArray<TSubclassOf<URPGBossGameplayAbility>> AbilityPatterns;

	/** Opt-in uniform random choice among already-granted, GAS-eligible patterns.
	 * Server only. False preserves the authored priority order; repeated choices are possible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns")
	bool bRandomizePatterns = false;

	/** Entry-based patterns. Mutually exclusive with the legacy class list. IDs are unique per phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns")
	TArray<FRPGBossPatternDefinition> Patterns;
	/** Sequence waits for the next entry to become eligible and advances only on successful completion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Patterns")
	ERPGBossPatternSelection PatternSelection = ERPGBossPatternSelection::Priority;

	/** Opt-in event gate for entering this phase. HP cannot cross this phase until
	 * the server calls AdvanceToPhase; the threshold still defines phase ordering.
	 * False preserves HP-only encounters. Not allowed on the initial phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	bool bRequiresExplicitTransition = false;

	/** Grant the existing GAS invincibility tag while this phase is active.
	 * Defaults off; does not prevent server-driven health assignment for reset/admin logic. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	bool bInvulnerable = false;

	/** Stop the boss's normal BT and movement during a mechanic phase.
	 * Server-triggered GAS mechanics and threat tracking remain available.
	 * Defaults off; a later combat phase restores only the AI/movement stopped by the encounter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	bool bSuspendCombatAI = false;

	/** Server phase time budget; zero disables it. Expiry fails the encounter,
	 * not a client timer or a lethal damage effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss", meta = (ClampMin = "0.0", ClampMax = "86400.0", Units = "s"))
	float TimeLimitSeconds = 0.f;

	/** Optional objective: defeat every member of the first successfully spawned
	 * wave in this phase, then enter this adjacent, explicitly gated phase.
	 * Empty disables it. Requires TimeLimitSeconds so failed/missing summons cannot stall forever.
	 * Does not cast a summon ability; BT or a server GAS mechanic still owns spawning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon")
	FGameplayTag SummonWaveCompletionPhase;
	/** Zero requires the entire wave. Otherwise defeat any N distinct members (1..8). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Summon", meta = (ClampMin = "0", ClampMax = "8"))
	int32 RequiredSummonDefeats = 0;

	/** Optional one-shot server GAS mechanic on phase entry. Must already be
	 * granted by NPC startup data. Requires suspended combat AI and a phase time limit.
	 * Waits for GAS activation eligibility, but never recasts after an activation attempt.
	 * Empty preserves existing BT-only phases. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Phase")
	TSubclassOf<URPGBossGameplayAbility> PhaseEntryAbility;
	/** Entry-based equivalent, including presentation and typed mechanic settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Phase")
	FRPGBossPatternDefinition PhaseEntryPattern;

	TSubclassOf<URPGBossGameplayAbility> GetPhaseEntryAbility() const
	{
		return PhaseEntryPattern.IsConfigured() ? PhaseEntryPattern.AbilityClass : PhaseEntryAbility;
	}
};

/** Immutable encounter configuration. Runtime state belongs to RPGBossAIComponent. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGBossDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TArray<FRPGBossPhaseDefinition> Phases;
	/** Entries using Show/Hide override this default. Never changes windup or damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Telegraph")
	bool bShowTelegraphsByDefault = true;

	/** Server combat time until automatic enrage. Zero disables the timer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Enrage", meta = (ClampMin = "0.0", ClampMax = "86400.0", Units = "s"))
	float EnrageSeconds = 0.f;

	/** GAS attack multiplier while enraged. One preserves legacy flag-only enrage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss|Enrage", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float EnragedAttackMultiplier = 1.f;

	bool ValidateDefinition(FText& OutError) const;
	int32 ResolvePhase(float HealthFraction, int32 CurrentPhase) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
