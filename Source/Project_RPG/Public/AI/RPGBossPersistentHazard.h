#pragma once

#include "GameFramework/Actor.h"
#include "AI/RPGBossAIComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Security/RPGSecurityTypes.h"
#include "TimerManager.h"
#include "RPGBossPersistentHazard.generated.h"

class APawn;
class AController;
class UAbilitySystemComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class URPGBossAIComponent;
class URPGBossThreatComponent;
class URPGBossPersistentHazardAbility;

/** Persistent state, not a multicast event. A late observer can reconstruct the remaining area. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossHazardState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Boss") TObjectPtr<APawn> SourcePawn = nullptr;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 EncounterId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 PhaseIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 CastSequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") FGameplayTag ActionTag;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") FVector Center = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") float Radius = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double EndServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") float PulseInterval = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 PulseCount = 0;
};

/** Fixed server area with instant GAS damage per pulse. Does not own or create another ASC. */
UCLASS(Blueprintable)
class PROJECT_RPG_API ARPGBossPersistentHazard : public AActor
{
	GENERATED_BODY()
public:
	ARPGBossPersistentHazard();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintPure, Category = "Boss|Hazard") FRPGBossHazardState GetHazardState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Boss|Hazard") bool IsPresentationActive() const;
	UFUNCTION(BlueprintPure, Category = "Boss|Hazard") bool IsIndicatorVisible() const;
	static bool IsTimingValid(float Duration, float Interval);
	/** Native server-only handoff from a committed, still-valid cast. Call before FinishSpawning. */
	bool InitializeFromCast(const URPGBossPersistentHazardAbility* Caster, FVector Center, int32 CastSequence,
		const FGameplayEffectSpecHandle& DamageSpec);
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(ReplicatedUsing = OnRep_State) FRPGBossHazardState State;
	UPROPERTY() FGameplayEffectSpecHandle DamageTemplate;
	UPROPERTY(Transient) TObjectPtr<UDecalComponent> Indicator;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> IndicatorMaterial;
	TWeakObjectPtr<URPGBossAIComponent> SourceState;
	TWeakObjectPtr<URPGBossThreatComponent> SourceThreat;
	TWeakObjectPtr<AController> SourceController;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	FRPGSkillSecurityProfile FrozenSecurity;
	FTimerHandle PulseTimer;
	FTimerHandle ExpiryTimer;
	bool bInitialized = false;
	bool bApplyingPulse = false;
	uint64 LastPulseFrame = MAX_uint64;
	bool IsServerContextValid() const;
	double ServerNow() const;
	void Pulse();
	void RefreshPresentation();
	UFUNCTION() void OnRep_State();
	UFUNCTION() void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
	UFUNCTION() void OnSourceDestroyed(AActor* Source);
	UFUNCTION() void OnSourceControllerChanged(APawn* Source, AController* Previous, AController* Current);
};
