#pragma once

#include "Components/ActorComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "RPGBossSummonObjectiveComponent.generated.h"

class AController;
class APawn;
class URPGBossSummonComponent;

/** Current objective only, not a history. Match encounter/phase against BossState
 * before displaying it: separate replicated components need not arrive in order. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossSummonObjectiveState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Boss") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 EncounterId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 PhaseIndex = INDEX_NONE;
	/** Zero means waiting for the first wave. */
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 RequiredCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 DefeatedCount = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGBossSummonObjectiveChanged,
	const FRPGBossSummonObjectiveState&, State);

/** Opt-in phase success policy. Summon ownership/lifetime remains in the registry;
 * phase deadlines/failure remain in BossAI, and damage remains in GAS. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossSummonObjectiveComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossSummonObjectiveComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category = "Boss|Summon")
	FRPGBossSummonObjectiveState GetObjectiveState() const { return Progress; }
	/** Presentation notification on server/clients. New subscribers must also read the snapshot. */
	UPROPERTY(BlueprintAssignable, Category = "Boss|Summon")
	FRPGBossSummonObjectiveChanged OnObjectiveChanged;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(ReplicatedUsing = OnRep_Progress) FRPGBossSummonObjectiveState Progress;
	TWeakObjectPtr<URPGBossAIComponent> BoundBoss;
	TWeakObjectPtr<URPGBossSummonComponent> BoundSummons;
	TWeakObjectPtr<AController> SourceController;
	FGameplayTag CompletionPhase;
	TSet<int32> DefeatedSlots;
	FTimerHandle CompletionTimer;

	bool IsContextCurrent() const;
	void SynchronizePhase();
	void CancelObjective();
	void Publish();
	void ScheduleCompletion(bool bRetry = false);
	void TryComplete(int32 EncounterId, int32 PhaseIndex, int32 Sequence);
	UFUNCTION() void OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& Current);
	UFUNCTION() void OnWaveSpawned(int32 EncounterId, int32 PhaseIndex, int32 Sequence, int32 MemberCount);
	UFUNCTION() void OnSummonDefeated(int32 EncounterId, int32 PhaseIndex, int32 Sequence, int32 Slot);
	UFUNCTION() void OnSourceControllerChanged(APawn* Pawn, AController* Previous, AController* Current);
	UFUNCTION() void OnRep_Progress();
};
