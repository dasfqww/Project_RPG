#pragma once

#include "CoreMinimal.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "RPGTrainingObjectiveComponent.generated.h"

class AController;
class ARPGPlayerState;
class UGameplayAbility;
class URPGAbilitySystemComponent;
class URPGSkillExecutionPolicy_Charge;
struct FAbilityEndedData;

/** Transient control practice, never a completed dungeon or a persistence receipt. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGTrainingObjectiveProgress
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) bool bBasicAttackCompleted = false;
	UPROPERTY(BlueprintReadOnly) bool bEvadeCompleted = false;
	UPROPERTY(BlueprintReadOnly) bool bAssaultCompleted = false;
	UPROPERTY(BlueprintReadOnly) bool bChargeCompleted = false;

	bool IsComplete() const
	{
		return bActive && bBasicAttackCompleted && bEvadeCompleted && bAssaultCompleted && bChargeCompleted;
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGTrainingObjectiveProgressChanged,
	const FRPGTrainingObjectiveProgress&, Progress);

/** Opt-in on a training Pawn. Observes server GAS; has no client completion setter/RPC. */
UCLASS(ClassGroup=(Progression), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGTrainingObjectiveComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGTrainingObjectiveComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Progression|Training")
	FRPGTrainingObjectiveProgress GetTrainingProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category="Progression|Training")
	bool IsControlPracticeComplete() const { return Progress.IsComplete(); }
	UPROPERTY(BlueprintAssignable, Category="Progression|Training")
	FRPGTrainingObjectiveProgressChanged OnTrainingProgressChanged;

private:
	enum class EGoal : uint8 { None, BasicAttack, Evade, Assault, Charge };
	struct FObservedCast
	{
		FGameplayAbilitySpecHandle Handle;
		EGoal Goal = EGoal::None;
		uint64 SkillActivation = 0;
		bool bCommitted = false;
		TWeakObjectPtr<const URPGSkillExecutionPolicy_Charge> ChargePolicy;
	};

	URPGAbilitySystemComponent* ResolveEligibleASC() const;
	bool IsObservationCurrent() const;
	bool IsCurrentGrant(const UGameplayAbility& Ability, FGameplayAbilitySpecHandle Handle) const;
	EGoal Classify(const UGameplayAbility& Ability) const;
	void RefreshObservation();
	void ClearObservation();
	void ObserveActivated(UGameplayAbility* Ability);
	void ObserveCommitted(UGameplayAbility* Ability);
	void ObserveEnded(const FAbilityEndedData& Ended);
	void Publish();
	UFUNCTION() void OnRep_Progress();

	UPROPERTY(ReplicatedUsing=OnRep_Progress) FRPGTrainingObjectiveProgress Progress;
	TWeakObjectPtr<URPGAbilitySystemComponent> ObservedASC;
	TWeakObjectPtr<ARPGPlayerState> ObservedPlayerState;
	TWeakObjectPtr<AController> ObservedController;
	FString ObservedSession, ObservedCharacter, ObservedSteamId;
	int64 ObservedConnectionGeneration = 0;
	uint32 ObservedRestoreGeneration = 0;
	FRPGApprovedHitContext PawnLifetime;
	TMap<TWeakObjectPtr<UGameplayAbility>, FObservedCast> Casts;
	FDelegateHandle ActivatedHandle, CommittedHandle, EndedHandle;
	bool bEnding = false, bRefreshing = false;
};
