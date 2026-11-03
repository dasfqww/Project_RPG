#pragma once
#include "AI/RPGBossTelegraphShape.h"

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "TimerManager.h"
#include "RPGBossAIComponent.generated.h"

class UAbilitySystemComponent;
class AController;
class APawn;
class URPGBossDefinition;
struct FRPGBossPhaseDefinition;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class ERPGBossEncounterState : uint8
{
	Idle,
	InCombat,
	Wiped,
	Resetting,
	Dead
};

/** A current-state snapshot, not an event history. Late observers read this directly. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	ERPGBossEncounterState EncounterState = ERPGBossEncounterState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	int32 EncounterId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	int32 Revision = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	int32 PhaseIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	FGameplayTag PhaseTag;

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	bool bEnraged = false;

	/** Automatic enrage deadline on the GameState server clock; zero when disabled/consumed. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	double EnrageServerTime = 0.0;

	/** Same clock as GameState::GetServerWorldTimeSeconds. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	double ServerStateTime = 0.0;

	/** Current phase deadline on the GameState server clock; zero when disabled/finished. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	double PhaseEndServerTime = 0.0;

	/** Persistent failure reason while Wiped; cleared by reset/new combat. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	bool bPhaseTimedOut = false;
};

/** Latest server-owned windup, readable by a late-joining client. Not a hit decision. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossTelegraphState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	int32 EncounterId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	int32 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	FGameplayTag ActionTag;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	FVector AimLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	FRPGBossTelegraphGeometry Geometry;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss")
	double EndServerTime = 0.0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPGBossStateChanged,
	const FRPGBossState&, PreviousState, const FRPGBossState&, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPGBossTelegraphChanged,
	const FRPGBossTelegraphState&, PreviousState, const FRPGBossTelegraphState&, NewState);

/** Opt-in boss lifecycle. Uses the owner's existing ASC; never creates a second ASC. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossAIComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossAIComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boss")
	TObjectPtr<URPGBossDefinition> Definition;

	UFUNCTION(BlueprintPure, Category = "Boss")
	FRPGBossState GetBossState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Boss")
	FRPGBossTelegraphState GetTelegraphState() const { return Telegraph; }

	/** Server-only frozen encounter data; never select from the editable source asset. */
	const FRPGBossPhaseDefinition* GetActivePhaseDefinition() const;

	/** Server and clients receive this; gameplay handlers must still check authority.
	 * New UI subscribers must read GetBossState before waiting for changes. */
	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FRPGBossStateChanged OnBossStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "Boss")
	FRPGBossTelegraphChanged OnTelegraphChanged;

	/** Return the action sequence; zero means rejected. A second action cannot replace an active one. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Telegraph")
	int32 BeginTelegraph(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds);
	/** Captures the exact server hit shape/transform atomically with the windup. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Telegraph")
	int32 BeginTelegraphWithShape(FGameplayTag ActionTag, const FTransform& QueryTransform,
		const FRPGHitQueryShape& Shape, float WindupSeconds);
	/** Stale callbacks cannot end a newer action. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Telegraph")
	bool EndTelegraph(int32 ExpectedEncounterId, int32 ExpectedSequence);

	/** Call after startup abilities/effects and the owner's ASC have finished initializing. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss")
	bool StartEncounter();

	/** Server mechanic completion/event hook, not a client RPC. Advances only to the
	 * immediate next phase authored with bRequiresExplicitTransition; no HP requirement.
	 * Capture encounter/current phase when the mechanic starts to reject stale callbacks.
	 * Uses the normal phase-change cleanup/replication; later HP-only phases may then resolve. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss|Phase")
	bool AdvanceToPhase(int32 ExpectedEncounterId, FGameplayTag ExpectedCurrentPhase, FGameplayTag NextPhase);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss")
	bool MarkWiped();

	/** Enter Resetting before the encounter owner cancels attacks, removes hazards and heals. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss")
	bool BeginReset();

	/** Caller certifies cleanup is finished. Requires positive health. Does not revive Dead. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss")
	bool CompleteReset();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Boss")
	bool SetEnraged(bool bNewEnraged);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	int32 BeginTelegraphInternal(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds,
		const FRPGBossTelegraphGeometry& Geometry);
	UPROPERTY(ReplicatedUsing = OnRep_State)
	FRPGBossState State;
	UPROPERTY(ReplicatedUsing = OnRep_Telegraph)
	FRPGBossTelegraphState Telegraph;

	/** Frozen at encounter start so live asset edits cannot corrupt an active phase. */
	UPROPERTY(Transient)
	TObjectPtr<URPGBossDefinition> ActiveDefinition;

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthChangedHandle;
	FDelegateHandle MaxHealthChangedHandle;
	bool bPublishingState = false;
	bool bPublishingTelegraph = false;
	FTimerHandle TelegraphTimeout;
	FTimerHandle EnrageTimeout;
	FTimerHandle PhaseTimeout;
	FTimerHandle PhaseEntryTimer;
	TWeakObjectPtr<AController> PhaseEntryController;
	TWeakObjectPtr<UAbilitySystemComponent> PhaseEntryASC;
	uint64 PhaseEntryGeneration = 0;
	FActiveGameplayEffectHandle EnrageEffectHandle;
	bool bUpdatingEnrage = false;
	bool bUpdatingPhaseProtection = false;
	FActiveGameplayEffectHandle PhaseInvulnerabilityHandle;

	bool CanMutate() const;
	double GetServerTime() const;
	bool HasPhaseTimeExpired() const;
	void StartPhaseTimeout();
	void ClearPhaseTimeout();
	void TryPhaseTimeout(int32 ExpectedEncounterId, int32 ExpectedPhaseIndex);
	void StartPhaseEntryAbility();
	void ClearPhaseEntryAbility();
	void TryPhaseEntryAbility(int32 ExpectedEncounterId, int32 ExpectedPhaseIndex, uint64 ExpectedGeneration);
	bool MarkWipedInternal(bool bDueToPhaseTimeout);
	void TryAutoEnrage(int32 ExpectedEncounterId);
	void ClearEnrage();
	bool EnsurePhaseInvulnerability(bool bEnabled);
	void ClearPhaseInvulnerability();
	bool BindAbilitySystem();
	void UnbindAbilitySystem();
	bool ReadHealth(float& OutHealth, float& OutMaxHealth) const;
	void OnHealthChanged(const FOnAttributeChangeData& Change);
	void EvaluateHealth();
	void PublishState(const FRPGBossState& PreviousState);
	void ClearTelegraph();
	void PublishTelegraph(const FRPGBossTelegraphState& PreviousState);

	UFUNCTION()
	void OnBossControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
	UFUNCTION()
	void OnRep_State(FRPGBossState PreviousState);
	UFUNCTION()
	void OnRep_Telegraph(FRPGBossTelegraphState PreviousState);
};
