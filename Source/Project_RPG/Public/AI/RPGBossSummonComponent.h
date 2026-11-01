#pragma once

#include "Components/ActorComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "RPGBossSummonComponent.generated.h"

class ARPGNonPlayerCharacter;
class AController;
class UAbilitySystemComponent;
class URPGBossGameplayAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRPGBossSummonWaveSpawned,
	int32, EncounterId, int32, PhaseIndex, int32, Sequence, int32, MemberCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FRPGBossSummonDefeated,
	int32, EncounterId, int32, PhaseIndex, int32, Sequence, int32, Slot);

/** Immutable cast input, retained by the ability through its windup (including GC references). */
USTRUCT()
struct PROJECT_RPG_API FRPGBossSummonPlan
{
	GENERATED_BODY()
	UPROPERTY() TSubclassOf<ARPGNonPlayerCharacter> PawnClass;
	UPROPERTY() TArray<FTransform> Transforms;
	UPROPERTY() float Lifetime = 30.f;
	UPROPERTY() float StartupTimeout = 10.f;
	UPROPERTY() int32 MaximumOwned = 8;
};

/** Current encounter-owned NPCs, including initializing NPCs and corpses until removal. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossSummonRecord
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Boss") TObjectPtr<ARPGNonPlayerCharacter> Pawn;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 EncounterId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 PhaseIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 Slot = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") bool bInitialized = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double EndServerTime = 0.0;
	TWeakObjectPtr<AController> Controller;
	double StartupDeadline = 0.0;
	bool bRetiring = false;
	bool bDefeated = false;
	TWeakObjectPtr<UAbilitySystemComponent> ObservedASC;
	FDelegateHandle HealthChangedHandle;
};

/** Owns NPC lifetimes, not a second ASC or a separate AI/BT implementation. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossSummonComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossSummonComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	UFUNCTION(BlueprintPure, Category = "Boss|Summon") TArray<FRPGBossSummonRecord> GetSummons() const { return Summons; }
	/** Server only, after every actor/controller in the wave was created.
	 * Async NPC startup can still be pending. Bind before casting; not a multicast RPC. */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Summon")
	FRPGBossSummonWaveSpawned OnWaveSpawned;
	/** Server only, once per initialized slot whose authoritative HP reaches zero.
	 * Expiry, failed startup, reset and live despawn are not kills. The mechanic owns
	 * count/deadline/failure policy and can call AdvanceToPhase after the required kills. */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Summon")
	FRPGBossSummonDefeated OnSummonDefeated;
	bool HasCapacity(int32 Count, int32 MaximumOwned) const;
	static bool IsPlanValid(const FRPGBossSummonPlan& Plan);
	bool SpawnWave(const URPGBossGameplayAbility* Caster, const FRPGBossSummonPlan& Plan, int32 Sequence);
	void RemoveWave(int32 EncounterId, int32 Sequence);
	void ClearSummons();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Replicated) TArray<FRPGBossSummonRecord> Summons;
	TWeakObjectPtr<URPGBossAIComponent> BoundBoss;
	TWeakObjectPtr<AController> SourceController;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC;
	uint8 SourceTeam = 255;
	uint64 MutationEpoch = 0;
	bool bSpawning = false;
	bool bCleaning = false;
	bool bNotifyingDefeat = false;
	double ServerNow() const;
	bool IsSourceValid() const;
	void UpdateSummons();
	void ObserveDefeat(ARPGNonPlayerCharacter* Pawn);
	void Retire(ARPGNonPlayerCharacter* Pawn);
	void DestroyRecords(const TArray<FRPGBossSummonRecord>& Records);
	void Publish();
	UFUNCTION() void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
	UFUNCTION() void OnSourceControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
	UFUNCTION() void OnMemberControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
	UFUNCTION() void OnMemberDestroyed(AActor* Actor);
	UFUNCTION() void OnMemberReady();
};
