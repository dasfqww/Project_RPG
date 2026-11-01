#pragma once

#include "CoreMinimal.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "RPGIntroApproachObjectiveComponent.generated.h"

class AController;
class ATriggerBox;
class ARPGPlayerState;
class UGameplayAbility;
class URPGAbilitySystemComponent;
struct FAbilityEndedData;

/** This attempt's owner-only practice state, never a persistent milestone/reward. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGIntroApproachProgress
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) bool bStartReached = false;
	UPROPERTY(BlueprintReadOnly) bool bApproachCompleted = false;
	UPROPERTY(BlueprintReadOnly) bool bAttackCompleted = false;
	bool IsComplete() const { return bActive && bStartReached && bApproachCompleted && bAttackCompleted; }
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPGIntroApproachProgressChanged,
	const FRPGIntroApproachProgress&, Progress);

/** Intro1-only server GAS observer. No completion setter/RPC and no reward authority. */
UCLASS(ClassGroup=(Progression), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGIntroApproachObjectiveComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGIntroApproachObjectiveComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Progression|Intro1")
	FRPGIntroApproachProgress GetApproachProgress() const { return Progress; }
	UFUNCTION(BlueprintPure, Category="Progression|Intro1")
	bool IsApproachPracticeComplete() const { return Progress.IsComplete(); }
	UPROPERTY(BlueprintAssignable, Category="Progression|Intro1")
	FRPGIntroApproachProgressChanged OnApproachProgressChanged;

private:
	friend struct FRPGIntroApproachObjectiveTestAccess;
	enum class EGoal : uint8 { None, Approach, Attack };
	struct FZone
	{
		TWeakObjectPtr<ATriggerBox> Actor;
		FTransform Transform;
		FVector Extent = FVector::ZeroVector;
	};
	struct FObservedCast
	{
		FGameplayAbilitySpecHandle Handle;
		EGoal Goal = EGoal::None;
		uint64 Activation = 0;
		FVector Start = FVector::ZeroVector;
		bool bCommitted = false;
	};

	URPGAbilitySystemComponent* ResolveEligibleASC() const;
	bool FindZones(TArray<FZone>& OutZones) const;
	bool IsLayoutCurrent() const;
	static bool Contains(const FZone& Zone, const FVector& Location);
	bool IsObservationCurrent() const;
	bool IsCurrentGrant(const UGameplayAbility& Ability, FGameplayAbilitySpecHandle Handle) const;
	EGoal Classify(const UGameplayAbility& Ability) const;
	bool CanStartGoal(EGoal Goal, const FVector& Location) const;
	bool CompleteSpatialGoal(const FObservedCast& Cast, const FVector& End);
	void RefreshObservation();
	void ClearObservation();
	void ObserveActivated(UGameplayAbility* Ability);
	void ObserveCommitted(UGameplayAbility* Ability);
	void ObserveEnded(const FAbilityEndedData& Ended);
	void Publish();
	UFUNCTION() void OnRep_Progress();

	UPROPERTY(ReplicatedUsing=OnRep_Progress) FRPGIntroApproachProgress Progress;
	TArray<FZone> Zones;
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
